package httpapi

// Spend controls for POST /llm/chat. Every accepted turn spends the OPERATOR's
// upstream — a paid API key, or a GPU someone else is queueing for — and a
// session token is valid for TOKEN_TTL_HOURS, so "authenticated" is not a
// budget. Two independent caps, both configurable, both 0 = unlimited:
//
//   - a per-session rate (LLM_RATE_PER_MINUTE), so one client can't loop; and
//   - a server-wide in-flight cap (LLM_MAX_IN_FLIGHT), which also bounds heap,
//     since each call in flight can hold an 8 MiB response plus its images.
//
// In-process only: a multi-instance deployment limits per instance. That is the
// honest scope — a shared counter belongs in the bus, not here.

import (
	"sync"
	"time"
)

// llmRateLimiter is a per-session token bucket: capacity = the per-minute rate,
// refilled continuously, so a client may burst up to a minute's worth and then
// settles to the configured pace.
type llmRateLimiter struct {
	mu      sync.Mutex
	perMin  float64
	buckets map[string]*rateBucket
	now     func() time.Time // test seam
}

type rateBucket struct {
	tokens float64
	last   time.Time
}

// idleBucketTTL is how long an untouched bucket is kept. Sessions come and go,
// so buckets are swept on use rather than growing for the process's lifetime.
const idleBucketTTL = 10 * time.Minute

func newLLMRateLimiter(perMin int) *llmRateLimiter {
	if perMin <= 0 {
		return nil // unlimited: no map, no lock, no sweep
	}
	return &llmRateLimiter{
		perMin:  float64(perMin),
		buckets: make(map[string]*rateBucket),
		now:     time.Now,
	}
}

// allow spends one token for id, reporting whether it was available. A nil
// limiter is unlimited, so callers need no special case.
func (l *llmRateLimiter) allow(id string) bool {
	if l == nil {
		return true
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	b := l.buckets[id]
	if b == nil {
		b = &rateBucket{tokens: l.perMin, last: now}
		l.buckets[id] = b
		l.sweep(now)
	} else {
		// Refill for the elapsed time, capped at one minute's worth.
		b.tokens += now.Sub(b.last).Minutes() * l.perMin
		if b.tokens > l.perMin {
			b.tokens = l.perMin
		}
		b.last = now
	}
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}

// sweep drops buckets nobody has touched recently. Called under the lock, only
// when a new session appears, so the cost lands on growth rather than on
// every request.
func (l *llmRateLimiter) sweep(now time.Time) {
	for id, b := range l.buckets {
		if now.Sub(b.last) > idleBucketTTL {
			delete(l.buckets, id)
		}
	}
}

// llmGate bounds concurrent upstream calls. The zero value (nil channel) is
// unlimited.
type llmGate struct{ slots chan struct{} }

func newLLMGate(n int) *llmGate {
	if n <= 0 {
		return &llmGate{}
	}
	return &llmGate{slots: make(chan struct{}, n)}
}

// enter takes a slot without blocking, returning a release func and whether one
// was free. Non-blocking on purpose: queueing behind a full gate would hold the
// client for the whole upstream timeout and answer late anyway.
func (g *llmGate) enter() (func(), bool) {
	if g == nil || g.slots == nil {
		return func() {}, true
	}
	select {
	case g.slots <- struct{}{}:
		return func() { <-g.slots }, true
	default:
		return nil, false
	}
}
