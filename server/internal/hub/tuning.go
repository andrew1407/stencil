package hub

import (
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/store"
)

// Tuning sizes a hub's deadlines and feed buffer. A zero field keeps the default.
type Tuning struct {
	OpTimeout     time.Duration // deadline around the hello's token lookup
	HelloTimeout  time.Duration // how long a fresh connection may take to send its hello
	NoticeTimeout time.Duration // one goodbye write (shutdown, token expiry), so a wedged peer cannot stall
	FeedBuffer    int           // events queued per connection before one drops (BUS_SUB_BUFFER)
}

var defaultTuning = Tuning{
	OpTimeout:     store.DefaultOpTimeout,
	HelloTimeout:  10 * time.Second,
	NoticeTimeout: time.Second,
	FeedBuffer:    eventbus.DefaultSubBuffer,
}

// defaultMaxConnsPerIP is the per-IP connection cap a hub built without WithMaxConnsPerIP keeps.
const defaultMaxConnsPerIP = 64

// WithMaxConnsPerIP caps the live connections one client IP may hold; 0 = unlimited.
func WithMaxConnsPerIP(n int) Option {
	return func(h *Hub) { h.perIP = newIPCounter(n) }
}

// WithTuning overrides the hub's sizing field by field.
func WithTuning(t Tuning) Option {
	return func(h *Hub) {
		if t.OpTimeout > 0 {
			h.tune.OpTimeout = t.OpTimeout
		}
		if t.HelloTimeout > 0 {
			h.tune.HelloTimeout = t.HelloTimeout
		}
		if t.NoticeTimeout > 0 {
			h.tune.NoticeTimeout = t.NoticeTimeout
		}
		if t.FeedBuffer > 0 {
			h.tune.FeedBuffer = t.FeedBuffer
		}
	}
}
