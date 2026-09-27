package hub

import (
	"errors"
	"log"
	"sync"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
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
	defer s.busStop()
	// The worker performs blocking store I/O off the run-loop and posts results back over persist.results; it
	// exits when s.done closes, so it is bounded by the session's lifetime and cannot leak.
	go s.persist.run()
	for {
		select {
		case m := <-s.register:
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
			s.fanout(env)
		case in := <-s.incoming:
			s.handle(in.member, in.msg)
		case v := <-s.written:
			s.refresh(v)
		case res := <-s.persist.results:
			s.applyResult(res)
		case <-s.done:
			for _, m := range s.members {
				close(m.out)
			}
			return
		}
	}
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

// handleSave dispatches the last-writer-wins UpdateProject to the worker; the outcome is applied back on
// the run-loop (applySaveResult) so version/state stay single-owner.
func (s *session) handleSave(m *member, msg protocol.WSMessage) {
	s.persist.dispatch(persistJob{kind: persistSave, member: m, layout: msg.Layout, version: msg.Version})
}

// applySaveResult applies a completed save on the run-loop: LWW/conflict/error handling, the version
// bump, the saver ack, the peer broadcast, and the global feed.
func (s *session) applySaveResult(res persistResult) {
	m := res.member
	switch {
	case errors.Is(res.err, store.ErrConflict):
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeConflict, Message: "stale version; reload"})
		}
		return
	case errors.Is(res.err, store.ErrNotFound):
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeNotFound, Message: "project gone"})
		}
		return
	case res.err != nil:
		log.Printf("hub: save project %s failed: %v", s.id, res.err)
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeInternal, Message: "save failed"})
		}
		return
	}
	rec := res.rec
	rec.Layout = s.loadedRec.Layout
	if len(res.layout) > 0 {
		rec.Layout = res.layout // an empty save leaves the stored layout, as the store's COALESCE does
	}
	s.version = rec.Version
	s.loadedRec = rec // keep the cached snapshot current with our own committed save
	// Ack the saver (if still connected) and broadcast the committed version to peers.
	if s.present(m) {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath})
	}
	synced := protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath, FromClientID: m.clientID}
	s.publish(synced)
	// Notify the global feed so projects lists refresh live.
	eventbus.PublishProjectEvent(s.hub.ctx, s.hub.bus, protocol.EventUpdated, rec)
}

// present reports whether m is still the registered member for its client id
// (its out channel is open). Run-loop-only, so it is atomic vs. unregister.
func (s *session) present(m *member) bool {
	cur, ok := s.members[m.clientID]
	return ok && cur == m
}
