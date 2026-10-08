package hub

// Frame delivery. Everything a session sends leaves through here: the local fan-out plus the bus publish
// that carries it to other instances, and the direct reply to one member.

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

// publish marshals msg, fans it out to the local members, and posts it to this project's bus channel for
// the other instances; this hub's own envelopes are skipped when they loop back.
func (s *session) publish(msg protocol.WSMessage) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	env := eventbus.EnvelopeOf(msg, data)
	s.fanout(env)
	env.Origin = s.hub.instance
	if err := s.hub.bus.Publish(s.hub.ctx, eventbus.ProjectChannel(s.id), env); err != nil {
		log.Printf("hub: publish to project %s failed: %v", s.id, err)
	}
}

func (s *session) sendMsg(m *member, msg protocol.WSMessage) {
	if data, err := json.Marshal(msg); err == nil {
		m.enqueue(data)
	}
}
