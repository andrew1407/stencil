package hub

import (
	"context"
	"encoding/json"
	"errors"
	"log"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/transport"
)

// member is one connected client within a session. The run-loop pushes outbound
// frames onto out; a per-member writeLoop drains it to the connection so a slow
// peer never blocks the run-loop.
type member struct {
	clientID string
	name     string
	conn     transport.Conn
	out      chan []byte
}

func (m *member) writeLoop(ctx context.Context, done chan struct{}) {
	defer close(done)
	for data := range m.out {
		if err := m.conn.Write(ctx, data); err != nil {
			// Drain remaining sends without writing so the run-loop's close of
			// out is observed and this goroutine exits.
			for range m.out {
			}
			return
		}
	}
}

// inbound couples a parsed message with the member that sent it.
type inbound struct {
	member *member
	msg    protocol.WSMessage
}

// persistKind tags an off-loop store operation dispatched to the worker.
type persistKind int

const (
	persistLoad persistKind = iota // GetProject (snapshot)
	persistSave                    // UpdateProject (save)
)

// persistJob is a unit of blocking store I/O handed from the run-loop to the
// worker goroutine. The run-loop never blocks on the store itself.
type persistJob struct {
	kind    persistKind
	member  *member         // save requester (for the ack/error reply); nil for load
	layout  json.RawMessage // save payload
	version int64           // save expected version (LWW guard)
}

// persistResult is the outcome of a persistJob, posted back to the run-loop so
// state mutations (version/snapshot) stay single-owner. It is applied on the
// run-loop, never by the worker.
type persistResult struct {
	kind   persistKind
	member *member
	rec    protocol.ProjectRecord
	err    error
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

	// jobs carries blocking store I/O to the worker; persistResults carries the
	// outcomes back to the run-loop's select.
	jobs           chan persistJob
	persistResults chan persistResult

	// run-loop-owned state
	members        map[string]*member
	version        int64
	loaded         bool
	loadInFlight   bool
	loadedRec      protocol.ProjectRecord // cached snapshot, kept current with our own saves
	pendingWelcome []*member              // members awaiting the initial load before their welcome
	busCh          <-chan []byte
	busStop        func()
}

func newSession(h *Hub, id string) *session {
	ch, stop := h.bus.Subscribe(bus.ProjectChannel(id))
	return &session{
		hub:            h,
		id:             id,
		register:       make(chan *member),
		unregister:     make(chan *member),
		incoming:       make(chan inbound),
		done:           make(chan struct{}),
		jobs:           make(chan persistJob, outBuffer),
		persistResults: make(chan persistResult, outBuffer),
		members:        map[string]*member{},
		busCh:          ch,
		busStop:        stop,
	}
}

// run is the session's sole goroutine. It serializes registration, inbound
// messages, and bus deliveries, so session state needs no locks.
func (s *session) run() {
	defer s.busStop()
	// The worker performs blocking store I/O off the run-loop and posts results
	// back over persistResults; it exits when s.done closes, so it is bounded by
	// the session's lifetime and cannot leak.
	go s.worker()
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
		case data := <-s.busCh:
			s.fanout(data)
		case in := <-s.incoming:
			s.handle(in.member, in.msg)
		case res := <-s.persistResults:
			s.applyResult(res)
		case <-s.done:
			for _, m := range s.members {
				close(m.out)
			}
			return
		}
	}
}

// worker is the session's second goroutine: it owns no session state, only
// draining jobs, running the blocking store call, and posting the outcome back
// to the run-loop. It exits when the session tears down.
func (s *session) worker() {
	for {
		select {
		case <-s.done:
			return
		case job := <-s.jobs:
			ctx, cancel := context.WithTimeout(s.hub.ctx, opTimeout)
			switch job.kind {
			case persistLoad:
				rec, err := s.hub.store.GetProject(ctx, s.id)
				s.postResult(persistResult{kind: persistLoad, rec: rec, err: err})
			case persistSave:
				rec, err := s.hub.store.UpdateProject(ctx, s.id, store.ProjectPatch{Layout: job.layout}, job.version)
				s.postResult(persistResult{kind: persistSave, member: job.member, rec: rec, err: err})
			}
			cancel()
		}
	}
}

// dispatch hands a job to the worker without blocking the run-loop past teardown.
func (s *session) dispatch(job persistJob) {
	select {
	case s.jobs <- job:
	case <-s.done:
	}
}

// postResult hands an outcome back to the run-loop; it unblocks if the session
// is already tearing down so the worker never leaks.
func (s *session) postResult(r persistResult) {
	select {
	case s.persistResults <- r:
	case <-s.done:
	}
}

// ensureLoaded kicks off the one-time snapshot load if it has not succeeded and
// is not already in flight. Idempotent; safe to call from any run-loop case.
func (s *session) ensureLoaded() {
	if s.loaded || s.loadInFlight {
		return
	}
	s.loadInFlight = true
	s.dispatch(persistJob{kind: persistLoad})
}

// applyResult applies an off-loop store outcome on the run-loop, keeping all
// state mutations single-owner.
func (s *session) applyResult(res persistResult) {
	switch res.kind {
	case persistLoad:
		s.loadInFlight = false
		if res.err != nil {
			// Load failed; leave loaded=false so a later join retries (never per
			// edit). Pending welcomes still get a reply below, with an empty
			// record + version 0 — the same content the old synchronous path sent
			// when GetProject errored.
			log.Printf("hub: load project %s failed: %v", s.id, res.err)
		} else {
			s.loadedRec = res.rec
			if !s.loaded {
				s.version = res.rec.Version
				s.loaded = true
			}
		}
		pending := s.pendingWelcome
		s.pendingWelcome = nil
		for _, m := range pending {
			s.replyWelcome(m)
		}
	case persistSave:
		s.applySaveResult(res)
	}
}

// fanout delivers a bus message to local members. Edit/cursor/presence frames
// are not echoed to their originator; lifecycle/ack frames go to everyone.
func (s *session) fanout(data []byte) {
	var msg protocol.WSMessage
	if json.Unmarshal(data, &msg) != nil {
		return
	}
	for id, m := range s.members {
		if echoSuppressed(msg.Type) && id == msg.FromClientID {
			continue
		}
		s.send(m, data)
	}
}

func echoSuppressed(t string) bool {
	return t == protocol.WSEdit || t == protocol.WSCursor || t == protocol.WSPresence
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
// The version is the one loaded once at first join — this never blocks on the
// store (the old per-edit lazy snapshot is gone).
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
	s.dispatch(persistJob{kind: persistSave, member: m, layout: msg.Layout, version: msg.Version})
}

// applySaveResult applies a completed save on the run-loop: LWW/conflict/error
// handling, the version bump, the saver ack, the peer broadcast, and the global
// feed — the same semantics and ordering as the old synchronous handleSave.
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

// publish marshals msg and posts it to this project's bus channel; the
// subscription loops it back to fanout (including this instance), which is the
// single delivery path to local members.
func (s *session) publish(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, bus.ProjectChannel(s.id), data); err != nil {
			log.Printf("hub: publish to project %s failed: %v", s.id, err)
		}
	}
}

// publishGlobal posts to the global events channel.
func (s *session) publishGlobal(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, bus.ChannelEvents, data); err != nil {
			log.Printf("hub: publish to global events failed: %v", err)
		}
	}
}

// send queues raw bytes to a member without blocking the run-loop.
func (s *session) send(m *member, data []byte) {
	select {
	case m.out <- data:
	default: // slow consumer; drop (recoverable via resubscribe)
	}
}

func (s *session) sendMsg(m *member, msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		s.send(m, data)
	}
}
