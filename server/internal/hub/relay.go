package hub

// Frame delivery. Everything a session sends leaves through here: the bus
// publish (which loops back to fanout, the single path to local members) and
// the direct reply to one member.

import (
	"encoding/json"
	"log"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
)

// fanout delivers a bus message to local members. Edit/cursor/presence frames
// are not echoed to their originator; lifecycle/ack frames go to everyone. The
// envelope carries the two fields routing needs, so the frame is never parsed
// here — at 10 peers relaying cursors that was 300 wasted unmarshals a second.
func (s *session) fanout(env bus.Envelope) {
	for id, m := range s.members {
		if echoSuppressed(env.Type) && id == env.From {
			continue
		}
		m.enqueue(env.Data)
	}
}

func echoSuppressed(t string) bool {
	return t == protocol.WSEdit || t == protocol.WSCursor || t == protocol.WSPresence
}

// publish marshals msg and posts it to this project's bus channel; the
// subscription loops it back to fanout (including this instance), which is the
// single delivery path to local members.
func (s *session) publish(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, bus.ProjectChannel(s.id), bus.EnvelopeOf(msg, data)); err != nil {
			log.Printf("hub: publish to project %s failed: %v", s.id, err)
		}
	}
}

// publishGlobal posts to the global events channel.
func (s *session) publishGlobal(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, bus.ChannelEvents, bus.EnvelopeOf(msg, data)); err != nil {
			log.Printf("hub: publish to global events failed: %v", err)
		}
	}
}

func (s *session) sendMsg(m *member, msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		m.enqueue(data)
	}
}
