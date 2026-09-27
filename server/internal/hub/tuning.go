package hub

import (
	"time"

	"stencil/server/internal/store"
)

// Tuning sizes a hub: its queues, one member's byte budget, and its deadlines. A zero field keeps
// the default.
type Tuning struct {
	OutBuffer      int           // frames queued per member, and per session's store arm
	OutBudgetBytes int64         // one member's queued backlog; a frame may be transport.MaxMessageBytes
	OpTimeout      time.Duration // deadline around one store call, the hello's token lookup included
	HelloTimeout   time.Duration // how long a fresh connection may take to send its hello
	NoticeTimeout  time.Duration // one goodbye write (shutdown, token expiry), so a wedged peer cannot stall
}

var defaultTuning = Tuning{
	OutBuffer:      256,
	OutBudgetBytes: 8 << 20, // one frame still always fits on an empty queue
	OpTimeout:      store.DefaultOpTimeout,
	HelloTimeout:   10 * time.Second,
	NoticeTimeout:  time.Second,
}

// WithTuning overrides the hub's sizing field by field.
func WithTuning(t Tuning) Option {
	return func(h *Hub) {
		if t.OutBuffer > 0 {
			h.tune.OutBuffer = t.OutBuffer
		}
		if t.OutBudgetBytes > 0 {
			h.tune.OutBudgetBytes = t.OutBudgetBytes
		}
		if t.OpTimeout > 0 {
			h.tune.OpTimeout = t.OpTimeout
		}
		if t.HelloTimeout > 0 {
			h.tune.HelloTimeout = t.HelloTimeout
		}
		if t.NoticeTimeout > 0 {
			h.tune.NoticeTimeout = t.NoticeTimeout
		}
	}
}
