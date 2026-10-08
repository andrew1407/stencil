package hub

import (
	"sync"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// inbound couples a parsed message with the member that sent it.
type inbound struct {
	member *member
	msg    protocol.WSMessage
}

// session is the authoritative, single-goroutine owner of a project's live edit state: every field below
// the channels is touched only by run() (the worker touches immutable fields + the job/result channels).
type session struct {
	hub     *Hub
	id      string
	refs    int       // guarded by Hub.mu
	started sync.Once // start: subscribe, then run

	register   chan *member
	unregister chan *member
	incoming   chan inbound
	written    chan int64 // one slot: the newest version the global feed reported (feed.go)
	done       chan struct{}
	deleted    chan struct{} // closed once the global feed reports the project deleted (feed.go)
	deleteOnce sync.Once
	exited     chan struct{} // closed when run has applied every committed save and returned

	// persist is the session's DB arm: blocking store I/O runs on its own
	// goroutine and comes back over persist.results (persist.go).
	persist *snapshotWorker

	// run-loop-owned state
	members        map[string]*member
	version        int64
	loaded         bool
	loadInFlight   bool
	stale          bool                   // a reported write no completed load has read yet
	reread         bool                   // that write was reported while a load ran
	loadedRec      protocol.ProjectRecord // cached snapshot: our own saves update it, others' re-read it
	pendingWelcome []*member              // members whose welcome waits for a load
	saveInFlight   bool                   // the worker holds a save; later ones wait in pendingSave
	pendingSave    *persistJob            // the newest save not yet dispatched
	gone           bool                   // the project no longer exists: every member is turned away
	busCh          <-chan eventbus.Envelope
	busStop        func()
}

func newSession(h *Hub, id string) *session {
	s := &session{
		hub:        h,
		id:         id,
		register:   make(chan *member),
		unregister: make(chan *member),
		incoming:   make(chan inbound),
		written:    make(chan int64, 1),
		done:       make(chan struct{}),
		deleted:    make(chan struct{}),
		exited:     make(chan struct{}),
		members:    map[string]*member{},
	}
	s.persist = newSnapshotWorker(h.ctx, h.store, id, h.tune, s.done)
	return s
}

// start subscribes to the project channel and launches run exactly once; a joiner arriving meanwhile
// waits here, so nobody registers before the subscription that delivers its frames exists.
func (s *session) start() {
	s.started.Do(func() {
		s.busCh, s.busStop = s.hub.bus.Subscribe(eventbus.ProjectChannel(s.id))
		go s.run()
	})
}

// run is the session's sole goroutine. It serializes registration, inbound
// messages, and bus deliveries, so session state needs no locks.
func (s *session) run() {
	defer s.hub.loopExited(s)
	defer s.busStop()
	go s.persist.run()
	deleted := s.deleted
	for {
		select {
		case m := <-s.register:
			if s.gone {
				s.evict(m)
				continue
			}
			s.members[m.clientID] = m
			// Load the snapshot/version once, at first join, off the run-loop.
			s.ensureLoaded()
			s.publish(protocol.WSMessage{Type: protocol.WSPeerJoin, ClientID: m.clientID, Name: m.name})
		case m := <-s.unregister:
			if cur, ok := s.members[m.clientID]; ok && cur == m {
				delete(s.members, m.clientID)
				close(m.out)
				s.publish(protocol.WSMessage{Type: protocol.WSPeerLeave, ClientID: m.clientID})
			}
		case env := <-s.busCh:
			if env.Origin != s.hub.instance {
				s.fanout(env) // this instance's own frames were fanned out when published
			}
		case in := <-s.incoming:
			if !in.member.evicted {
				s.handle(in.member, in.msg)
			}
		case v := <-s.written:
			s.refresh(v)
		case res := <-s.persist.results:
			s.applyResult(res)
		case <-deleted:
			deleted = nil
			s.endProject()
		case <-s.done:
			s.teardown()
			return
		}
	}
}

// teardown runs once the last member has left: it still commits a save the worker holds or one waiting
// behind it, and applies each outcome, so the global feed announces every committed save.
func (s *session) teardown() {
	for _, m := range s.members {
		close(m.out)
	}
	s.members = map[string]*member{}
	if job := s.pendingSave; job != nil {
		s.pendingSave = nil
		s.persist.dispatch(*job)
	}
	close(s.persist.jobs)
	for res := range s.persist.results {
		if res.kind == persistSave {
			s.applySaveResult(res)
		}
	}
}

// projectGone is the last frame a member gets once its project no longer exists.
var projectGone = protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeNotFound, Message: "project not found"}

// endProject turns every member away once the store or the feed says the project is gone.
func (s *session) endProject() {
	s.gone = true
	s.pendingWelcome = nil
	for _, m := range s.members {
		s.evict(m)
	}
}

// evict tells m its project is gone and drops it; its writer flushes the notice, then hangs up.
func (s *session) evict(m *member) {
	s.sendMsg(m, projectGone)
	if s.present(m) {
		delete(s.members, m.clientID)
	}
	m.evicted = true
	m.hangUp.Store(true)
	close(m.out)
}

// handle dispatches one inbound message from a member.
func (s *session) handle(m *member, msg protocol.WSMessage) {
	switch msg.Type {
	case protocol.WSSubscribe:
		s.sendWelcome(m)
	case protocol.WSEdit:
		s.handleEdit(m, msg)
	case protocol.WSCursor, protocol.WSPresence:
		msg.FromClientID = m.clientID
		s.publish(msg) // ephemeral relay, not persisted
	case protocol.WSSave:
		s.handleSave(m, msg)
	case protocol.WSPing:
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSPong})
	}
}

// sendWelcome replies with project + layout + version + the local peer roster. Before the snapshot loads,
// or while a refresh re-reads it, the reply waits for the run-loop's load result; no store I/O runs here.
func (s *session) sendWelcome(m *member) {
	if !s.loaded || s.stale {
		s.ensureLoaded()
		s.pendingWelcome = append(s.pendingWelcome, m)
		return
	}
	s.replyWelcome(m)
}

// replyWelcome sends the welcome frame from the cached snapshot. The member may have disconnected while
// the load was in flight, so it is skipped if no longer present (its out channel would be closed).
func (s *session) replyWelcome(m *member) {
	if !s.present(m) {
		return
	}
	peers := make([]protocol.Peer, 0, len(s.members))
	for id, mem := range s.members {
		peers = append(peers, protocol.Peer{ClientID: id, Name: mem.name})
	}
	rec := s.loadedRec
	rec.Layout = nil // the snapshot rides once, as the frame's own layout
	s.sendMsg(m, protocol.WSMessage{
		Type:    protocol.WSWelcome,
		Project: &rec,
		Layout:  s.loadedRec.Layout,
		Version: s.version,
		Peers:   peers,
	})
}

// handleEdit relays a live edit op to peers, unpersisted. A version behind the session's (absent = 0, so
// stale once a save has landed) means the sender is behind, so it is told to resync instead.
func (s *session) handleEdit(m *member, msg protocol.WSMessage) {
	if msg.Version < s.version {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeBadVersion, Message: "stale; resubscribe"})
		return
	}
	msg.FromClientID = m.clientID
	s.publish(msg)
}
