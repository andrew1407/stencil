package hub

import (
	"errors"
	"log"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// inbound couples a parsed message with the member that sent it.
type inbound struct {
	member *member
	msg    protocol.WSMessage
}

// session is the authoritative, single-goroutine owner of a project's live edit
// state. All fields below the channels are touched only by run() (the worker
// goroutine touches only immutable fields + the job/result channels).
type session struct {
	hub  *Hub
	id   string
	refs int // guarded by Hub.mu

	register   chan *member
	unregister chan *member
	incoming   chan inbound
	done       chan struct{}

	// persist is the session's DB arm: blocking store I/O runs on its own
	// goroutine and comes back over persist.results (persist.go).
	persist *snapshotWorker

	// run-loop-owned state
	members        map[string]*member
	version        int64
	loaded         bool
	loadInFlight   bool
	loadedRec      protocol.ProjectRecord // cached snapshot, kept current with our own saves
	pendingWelcome []*member              // members awaiting the initial load before their welcome
	busCh          <-chan eventbus.Envelope
	busStop        func()
}

func newSession(h *Hub, id string) *session {
	ch, stop := h.bus.Subscribe(eventbus.ProjectChannel(id))
	s := &session{
		hub:        h,
		id:         id,
		register:   make(chan *member),
		unregister: make(chan *member),
		incoming:   make(chan inbound),
		done:       make(chan struct{}),
		members:    map[string]*member{},
		busCh:      ch,
		busStop:    stop,
	}
	s.persist = newSnapshotWorker(h.ctx, h.store, id, opTimeout, s.done)
	return s
}

// run is the session's sole goroutine. It serializes registration, inbound
// messages, and bus deliveries, so session state needs no locks.
func (s *session) run() {
	defer s.busStop()
	// The worker performs blocking store I/O off the run-loop and posts results
	// back over persist.results; it exits when s.done closes, so it is bounded by
	// the session's lifetime and cannot leak.
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

// sendWelcome replies with project + layout + version + the current local peer
// roster. If the one-time snapshot has not loaded yet, the reply is deferred
// until the load result arrives on the run-loop (see applyResult), so no store
// I/O ever runs here.
func (s *session) sendWelcome(m *member) {
	if !s.loaded {
		s.ensureLoaded()
		s.pendingWelcome = append(s.pendingWelcome, m)
		return
	}
	s.replyWelcome(m)
}

// replyWelcome sends the welcome frame from the cached snapshot. The member may
// have disconnected while the load was in flight, so it is skipped if no longer
// present (its out channel would be closed).
func (s *session) replyWelcome(m *member) {
	if !s.present(m) {
		return
	}
	peers := make([]protocol.Peer, 0, len(s.members))
	for id, mem := range s.members {
		peers = append(peers, protocol.Peer{ClientID: id, Name: mem.name})
	}
	rec := s.loadedRec
	s.sendMsg(m, protocol.WSMessage{
		Type:    protocol.WSWelcome,
		Project: &rec,
		Layout:  rec.Layout,
		Version: s.version,
		Peers:   peers,
	})
}

// handleEdit relays a live edit op to peers. Edits are ephemeral (not persisted
// per-op); a stale version means the sender is behind, so it is told to resync.
// The version is the one loaded once at first join, so this never blocks on the
// store.
func (s *session) handleEdit(m *member, msg protocol.WSMessage) {
	if msg.Version != 0 && msg.Version < s.version {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeBadVersion, Message: "stale; resubscribe"})
		return
	}
	msg.FromClientID = m.clientID
	s.publish(msg)
}

// handleSave dispatches the last-writer-wins UpdateProject to the worker; the
// outcome is applied back on the run-loop (applySaveResult) so version/state
// stay single-owner and the save never blocks other members' relays.
func (s *session) handleSave(m *member, msg protocol.WSMessage) {
	s.persist.dispatch(persistJob{kind: persistSave, member: m, layout: msg.Layout, version: msg.Version})
}

// applySaveResult applies a completed save on the run-loop: LWW/conflict/error
// handling, the version bump, the saver ack, the peer broadcast, and the global
// feed.
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
	s.version = rec.Version
	s.loadedRec = rec // keep the cached snapshot current with our own committed save
	// Ack the saver (if still connected) and broadcast the committed version to peers.
	if s.present(m) {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath})
	}
	synced := protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath, FromClientID: m.clientID}
	s.publish(synced)
	// Notify the global feed so projects lists refresh live.
	s.publishGlobal(protocol.WSMessage{Type: protocol.WSProjectEv, Event: protocol.EventUpdated, Project: &rec})
}

// present reports whether m is still the registered member for its client id
// (its out channel is open). Run-loop-only, so it is atomic vs. unregister.
func (s *session) present(m *member) bool {
	cur, ok := s.members[m.clientID]
	return ok && cur == m
}
