// Package bus is the publish/subscribe abstraction the hub uses to fan edit and
// project events out to every connection — across server instances when backed
// by Redis, or within one process via the in-memory implementation here. The
// hub depends only on the Bus interface, so the transport (WS or TCP) and the
// backend (Redis or in-proc) are both swappable.
package bus

import (
	"context"
	"sync"
)

// Channel names used across the server.
const (
	// ChannelEvents is the global project-lifecycle feed (created/updated/deleted).
	ChannelEvents = "events"
)

// ProjectChannel returns the per-project edit/presence channel name.
func ProjectChannel(projectID string) string { return "proj:" + projectID }

// Bus is a minimal pub/sub contract.
type Bus interface {
	// Publish sends data to every current subscriber of channel.
	Publish(ctx context.Context, channel string, data []byte) error
	// Subscribe returns a receive channel of messages and an unsubscribe func.
	// The returned channel is closed when unsubscribe is called; the subscription
	// lives until then (it is not bound to a per-call context).
	Subscribe(channel string) (<-chan []byte, func())
	// Close releases any backend resources.
	Close() error
}

// subBuffer bounds per-subscriber queueing; a slow consumer drops messages
// rather than stalling the publisher (edit state is reconciled by version, so a
// dropped relay is recoverable).
const subBuffer = 64

// InProc is an in-memory Bus for single-instance deployments and tests.
type InProc struct {
	mu   sync.Mutex
	subs map[string]map[int]chan []byte
	next int
}

// NewInProc creates an in-memory bus.
func NewInProc() *InProc {
	return &InProc{subs: make(map[string]map[int]chan []byte)}
}

// Publish delivers data to every subscriber of channel without blocking.
func (b *InProc) Publish(_ context.Context, channel string, data []byte) error {
	b.mu.Lock()
	defer b.mu.Unlock()
	for _, ch := range b.subs[channel] {
		select {
		case ch <- data:
		default: // subscriber is behind; drop (recoverable via version resync)
		}
	}
	return nil
}

// Subscribe registers a new subscriber for channel.
func (b *InProc) Subscribe(channel string) (<-chan []byte, func()) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.subs[channel] == nil {
		b.subs[channel] = make(map[int]chan []byte)
	}
	id := b.next
	b.next++
	ch := make(chan []byte, subBuffer)
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

// Close is a no-op for the in-memory bus.
func (b *InProc) Close() error { return nil }
