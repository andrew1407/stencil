package hub

import (
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/store"
)

// Tuning sizes a hub: its deadlines and the per-IP connection cap. A zero field keeps the default.
type Tuning struct {
	OpTimeout     time.Duration // deadline around the hello's token lookup
	HelloTimeout  time.Duration // how long a fresh connection may take to send its hello
	NoticeTimeout time.Duration // one goodbye write (shutdown, token expiry), so a wedged peer cannot stall
	MaxConnsPerIP int           // live connections one client IP may hold; 0 = unlimited
	FeedBuffer    int           // events queued per connection before one drops (BUS_SUB_BUFFER)
}

var defaultTuning = Tuning{
	OpTimeout:     store.DefaultOpTimeout,
	HelloTimeout:  10 * time.Second,
	NoticeTimeout: time.Second,
	MaxConnsPerIP: 64,
	FeedBuffer:    eventbus.DefaultSubBuffer,
}

// WithTuning overrides the hub's sizing field by field; a negative MaxConnsPerIP lifts the cap.
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
		if t.MaxConnsPerIP != 0 {
			h.tune.MaxConnsPerIP = max(t.MaxConnsPerIP, 0)
		}
	}
}
