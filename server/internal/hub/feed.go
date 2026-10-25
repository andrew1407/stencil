package hub

// The global feed's in-process fan-out: the hub holds the one bus subscription to `events` (one Redis
// connection however many clients listen) and copies each frame onto every listener's bounded channel.

import (
	"encoding/json"
	"sync"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
	"stencil/server/internal/validate"
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
			if env.Type == eventbus.TypeSessionRevoked {
				var id string
				if json.Unmarshal(env.Data, &id) == nil {
					h.revoke(id)
				}
				continue
			}
			if forwardable(env) {
				h.feed.deliver(env.Data)
			}
		}
	}
}

// forwardable admits only what this server publishes: a project event no bigger than a hello frame,
// naming a well-formed project id. The bus (Redis) sits inside the trust boundary, but a frame from it
// reaches every client, so it is checked like client input.
func forwardable(env eventbus.Envelope) bool {
	if env.Type != protocol.WSProjectEv || len(env.Data) > transport.MaxHelloBytes {
		return false
	}
	var msg struct {
		Type    string `json:"type"`
		Project *struct {
			ID string `json:"id"`
		} `json:"project"`
	}
	if json.Unmarshal(env.Data, &msg) != nil || msg.Type != protocol.WSProjectEv || msg.Project == nil {
		return false
	}
	return validate.ProjectID(msg.Project.ID)
}
