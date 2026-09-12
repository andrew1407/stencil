// Package bus is the publish/subscribe abstraction the hub uses to fan edit and
// project events out to every connection — across server instances when backed
// by Redis, or within one process via the in-memory implementation here. The
// hub depends only on the Bus interface, so the transport (WS or TCP) and the
// backend (Redis or in-proc) are both swappable.
package bus

import (
	"context"
	"encoding/json"
	"sync"

	"stencil/server/internal/protocol"
)

// Channel names used across the server.
const (
	// ChannelEvents is the global project-lifecycle feed (created/updated/deleted).
	ChannelEvents = "events"
)

// ProjectChannel returns the per-project edit/presence channel name.
func ProjectChannel(projectID string) string { return "proj:" + projectID }

// Envelope is one bus message: the marshalled frame plus the two header fields a
// receiver routes on, so fan-out never re-parses the frame to read them.
type Envelope struct {
	Type string          `json:"type"`
	From string          `json:"from,omitempty"` // originating client id ("" = server)
	Data json.RawMessage `json:"data"`           // the frame as sent to clients
}

// EnvelopeOf wraps an already-marshalled frame for publication.
func EnvelopeOf(msg protocol.WSMessage, data []byte) Envelope {
	return Envelope{Type: msg.Type, From: msg.FromClientID, Data: data}
}

// Bus is a minimal pub/sub contract.
type Bus interface {
	// Publish sends env to every current subscriber of channel.
	Publish(ctx context.Context, channel string, env Envelope) error
	// Subscribe returns a receive channel of messages and an unsubscribe func.
	// The returned channel is closed when unsubscribe is called; the subscription
	// lives until then (it is not bound to a per-call context).
	Subscribe(channel string) (<-chan Envelope, func())
	// Close releases any backend resources.
	Close() error
}

// PublishProjectEvent broadcasts a project-lifecycle event on the global feed —
// the single path for it, so a swept project looks exactly like a manual delete.
func PublishProjectEvent(ctx context.Context, b Bus, event string, rec protocol.ProjectRecord) {
	if b == nil {
		return
	}
	msg := protocol.WSMessage{Type: protocol.WSProjectEv, Event: event, Project: &rec}
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	_ = b.Publish(ctx, ChannelEvents, EnvelopeOf(msg, data))
}

// subBuffer bounds per-subscriber queueing; a slow consumer drops messages
// rather than stalling the publisher (edit state is reconciled by version, so a
// dropped relay is recoverable).
const subBuffer = 64

// inProc is an in-memory Bus for single-instance deployments and tests.
type inProc struct {
	drops DropLog

	mu   sync.Mutex
	subs map[string]map[int]chan Envelope
	next int
}

// NewInProc creates an in-memory bus.
func NewInProc() Bus {
	return &inProc{subs: make(map[string]map[int]chan Envelope)}
}

// Publish delivers env to every subscriber of channel without blocking.
func (b *inProc) Publish(_ context.Context, channel string, env Envelope) error {
	b.mu.Lock()
	defer b.mu.Unlock()
	for _, ch := range b.subs[channel] {
		select {
		case ch <- env:
		default:
			b.drops.Drop("bus", channel)
		}
	}
	return nil
}

// Subscribe registers a new subscriber for channel.
func (b *inProc) Subscribe(channel string) (<-chan Envelope, func()) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.subs[channel] == nil {
		b.subs[channel] = make(map[int]chan Envelope)
	}
	id := b.next
	b.next++
	ch := make(chan Envelope, subBuffer)
	b.subs[channel][id] = ch

	var once sync.Once
	cancel := func() {
		once.Do(func() {
			b.mu.Lock()
			defer b.mu.Unlock()
			if m := b.subs[channel]; m != nil {
				if c, ok := m[id]; ok {
					delete(m, id)
					close(c)
				}
				if len(m) == 0 {
					delete(b.subs, channel)
				}
			}
		})
	}
	return ch, cancel
}

func (b *inProc) Close() error { return nil }
