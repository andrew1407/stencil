package hub

// The global feed's in-process fan-out: the hub holds the one bus subscription to `events` (one Redis
// connection however many clients listen) and copies each frame onto every listener's bounded channel.

import (
	"sync"

	"stencil/server/internal/eventbus"
)

// feed is the set of listening connections. A closed feed hands out closed channels, so a connection that
// arrives after the bus subscription ended returns at once.
type feed struct {
	buffer int
	drops  eventbus.DropLog

	mu     sync.Mutex
	subs   map[chan []byte]struct{}
	closed bool
}

func newFeed(buffer int) *feed {
	return &feed{buffer: max(buffer, 1), subs: map[chan []byte]struct{}{}}
}

// listen registers a listener; the returned func unregisters it.
func (f *feed) listen() (<-chan []byte, func()) {
	ch := make(chan []byte, f.buffer)
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.closed {
		close(ch)
		return ch, func() {}
	}
	f.subs[ch] = struct{}{}
	return ch, func() {
		f.mu.Lock()
		defer f.mu.Unlock()
		if _, ok := f.subs[ch]; ok {
			delete(f.subs, ch)
			close(ch)
		}
	}
}

// deliver copies one frame to every listener without blocking; a full listener drops it.
func (f *feed) deliver(data []byte) {
	f.mu.Lock()
	defer f.mu.Unlock()
	for ch := range f.subs {
		select {
		case ch <- data:
		default:
			f.drops.Drop("hub", eventbus.ChannelEvents)
		}
	}
}

// close ends every listener once the bus subscription is gone.
func (f *feed) close() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.closed = true
	for ch := range f.subs {
		delete(f.subs, ch)
		close(ch)
	}
}

// pump forwards the hub's one subscription into the feed until the hub closes or the bus ends it.
func (h *Hub) pump(events <-chan eventbus.Envelope, stop func()) {
	defer h.feed.close()
	defer stop()
	for {
		select {
		case <-h.ctx.Done():
			return
		case env, ok := <-events:
			if !ok {
				return
			}
			h.feed.deliver(env.Data)
		}
	}
}
