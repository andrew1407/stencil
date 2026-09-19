package hub

// Frame delivery. Everything a session sends leaves through here: the bus
// publish (which loops back to fanout, the single path to local members) and
// the direct reply to one member.

import (
	"encoding/json"
	"log"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// fanout delivers a bus message to local members: edit/cursor/presence are not echoed to their originator,
// lifecycle/ack go to everyone. The envelope carries the routing fields, so no frame is parsed here.
func (s *session) fanout(env eventbus.Envelope) {
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

// publish marshals msg and posts it to this project's bus channel; the subscription loops it back to
// fanout (including this instance), the single delivery path to local members.
func (s *session) publish(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, eventbus.ProjectChannel(s.id), eventbus.EnvelopeOf(msg, data)); err != nil {
			log.Printf("hub: publish to project %s failed: %v", s.id, err)
		}
	}
}

// publishGlobal posts to the global events channel.
func (s *session) publishGlobal(msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		if err := s.hub.bus.Publish(s.hub.ctx, eventbus.ChannelEvents, eventbus.EnvelopeOf(msg, data)); err != nil {
			log.Printf("hub: publish to global events failed: %v", err)
		}
	}
}

func (s *session) sendMsg(m *member, msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		m.enqueue(data)
	}
}
