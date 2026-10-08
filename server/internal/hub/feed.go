package hub

// The hub's subscription to the global feed. A write no session of this hub made — a REST upload or
// update, a save on another instance — is announced there, and the live session it names re-reads.

import (
	"encoding/json"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// watchFeed hands each `updated` project-event's version to that project's live session on this
// instance, and a `deleted` one ends it, until the hub's context ends; stop releases the subscription.
func (h *Hub) watchFeed(events <-chan eventbus.Envelope, stop func()) {
	defer stop()
	for {
		select {
		case <-h.ctx.Done():
			return
		case env, ok := <-events:
			if !ok {
				return
			}
			if env.Type != protocol.WSProjectEv {
				continue
			}
			var msg protocol.WSMessage
			if json.Unmarshal(env.Data, &msg) != nil || msg.Project == nil {
				continue
			}
			h.mu.Lock()
			s := h.sessions[msg.Project.ID]
			h.mu.Unlock()
			switch {
			case s == nil:
			case msg.Event == protocol.EventUpdated:
				s.noteWrite(msg.Project.Version)
			case msg.Event == protocol.EventDeleted:
				s.deleteOnce.Do(func() { close(s.deleted) })
			}
		}
	}
}

// noteWrite passes the run-loop the newest version a write reached, never blocking the feed: a
// version still waiting in the one-slot channel is merged with this one, the larger kept.
func (s *session) noteWrite(v int64) {
	for {
		select {
		case s.written <- v:
			return
		default:
		}
		select {
		case old := <-s.written:
			v = max(v, old)
		default:
		}
	}
}
