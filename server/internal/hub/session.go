package hub

import (
	"context"
	"encoding/json"
	"errors"

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

// session is the authoritative, single-goroutine owner of a project's live edit
// state. All fields below the channels are touched only by run().
type session struct {
	hub  *Hub
	id   string
	refs int // guarded by Hub.mu

	register   chan *member
	unregister chan *member
	incoming   chan inbound
	done       chan struct{}

	// run-loop-owned state
	members map[string]*member
	version int64
	loaded  bool
	busCh   <-chan []byte
	busStop func()
}

func newSession(h *Hub, id string) *session {
	ch, stop := h.bus.Subscribe(h.ctx, bus.ProjectChannel(id))
	return &session{
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
}

// run is the session's sole goroutine. It serializes registration, inbound
// messages, and bus deliveries, so session state needs no locks.
func (s *session) run() {
	defer s.busStop()
	for {
		select {
		case m := <-s.register:
			s.members[m.clientID] = m
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
		case <-s.done:
			for _, m := range s.members {
				close(m.out)
			}
			return
		}
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

// sendWelcome loads the durable snapshot (once) and replies with project +
// layout + version + the current local peer roster.
func (s *session) sendWelcome(m *member) {
	rec := s.snapshot()
	peers := make([]protocol.Peer, 0, len(s.members))
	for id, mem := range s.members {
		peers = append(peers, protocol.Peer{ClientID: id, Name: mem.name})
	}
	s.sendMsg(m, protocol.WSMessage{
		Type:    protocol.WSWelcome,
		Project: &rec,
		Layout:  rec.Layout,
		Version: s.version,
		Peers:   peers,
	})
}

// snapshot lazily loads the project record/version from the store.
func (s *session) snapshot() protocol.ProjectRecord {
	ctx, cancel := context.WithTimeout(s.hub.ctx, opTimeout)
	defer cancel()
	rec, err := s.hub.store.GetProject(ctx, s.id)
	if err == nil && !s.loaded {
		s.version = rec.Version
		s.loaded = true
	}
	return rec
}

// handleEdit relays a live edit op to peers. Edits are ephemeral (not persisted
// per-op); a stale version means the sender is behind, so it is told to resync.
func (s *session) handleEdit(m *member, msg protocol.WSMessage) {
	if !s.loaded {
		s.snapshot()
	}
	if msg.Version != 0 && msg.Version < s.version {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeBadVersion, Message: "stale; resubscribe"})
		return
	}
	msg.FromClientID = m.clientID
	s.publish(msg)
}

// handleSave persists the full layout under a last-writer-wins version guard and
// broadcasts the new authoritative version.
func (s *session) handleSave(m *member, msg protocol.WSMessage) {
	ctx, cancel := context.WithTimeout(s.hub.ctx, opTimeout)
	defer cancel()
	rec, err := s.hub.store.UpdateProject(ctx, s.id, nil, nil, nil, msg.Layout, msg.Version)
	switch {
	case errors.Is(err, store.ErrConflict):
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeConflict, Message: "stale version; reload"})
		return
	case errors.Is(err, store.ErrNotFound):
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeNotFound, Message: "project gone"})
		return
	case err != nil:
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeInternal, Message: "save failed"})
		return
	}
	s.version = rec.Version
	// Ack the saver and broadcast the committed version to peers.
	synced := protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath}
	s.sendMsg(m, synced)
	synced.FromClientID = m.clientID
	s.publish(synced)
	// Notify the global feed so projects lists refresh live.
	s.publishGlobal(protocol.WSMessage{Type: protocol.WSProjectEv, Event: protocol.EventUpdated, Project: &rec})
}

// publish marshals msg and posts it to this project's bus channel; the
// subscription loops it back to fanout (including this instance), which is the
// single delivery path to local members.
func (s *session) publish(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		_ = s.hub.bus.Publish(s.hub.ctx, bus.ProjectChannel(s.id), data)
	}
}

// publishGlobal posts to the global events channel.
func (s *session) publishGlobal(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		_ = s.hub.bus.Publish(s.hub.ctx, bus.ChannelEvents, data)
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
