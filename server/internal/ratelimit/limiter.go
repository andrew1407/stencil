// Package ratelimit holds the server's one token bucket and the client key it is
// spent against. Every metered surface uses it: token issuance and REST writes
// (httpapi/ratelimit.go), /llm/chat spend (httpapi/llmlimit.go), and failed
// WS/TCP hello handshakes (hub). In-process only — a multi-instance deployment
// limits per instance, which is the honest scope for a counter that lives here.
package ratelimit

import (
	"sync"
	"time"
)

// IdleTTL is how long an untouched bucket is kept. Keys come and go, so buckets
// are swept on use rather than growing for the process's lifetime.
const IdleTTL = 10 * time.Minute

// Limiter is a per-key token bucket: capacity = the per-minute rate, refilled continuously, so a caller
// may burst up to a minute's worth and then settles to the configured pace.
type Limiter struct {
	mu      sync.Mutex
	perMin  float64
	buckets map[string]*bucket
	now     func() time.Time // test seam
}

type bucket struct {
	tokens float64
	last   time.Time
}

// New returns a limiter of perMin spends per key per minute. perMin <= 0 yields
// nil: unlimited, with no map, no lock and no sweep.
func New(perMin int) *Limiter {
	if perMin <= 0 {
		return nil
	}
	return &Limiter{
		perMin:  float64(perMin),
		buckets: make(map[string]*bucket),
		now:     time.Now,
	}
}

// Allow spends one token for key, reporting whether it was available. A nil
// limiter is unlimited, so callers need no special case.
func (l *Limiter) Allow(key string) bool {
	if l == nil {
		return true
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	b := l.buckets[key]
	if b == nil {
		b = &bucket{tokens: l.perMin, last: now}
		l.buckets[key] = b
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

// Refund returns one spent token to key's bucket, never above capacity: spend before the work, refund
// when the work turns out to have been legitimate.
func (l *Limiter) Refund(key string) {
	if l == nil {
		return
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	b := l.buckets[key]
	if b == nil {
		return
	}
	b.tokens++
	if b.tokens > l.perMin {
		b.tokens = l.perMin
	}
}

// sweep drops buckets nobody has touched within IdleTTL. Called under the lock, only when a new key
// appears, so the cost lands on growth rather than on every spend.
func (l *Limiter) sweep(now time.Time) {
	for key, b := range l.buckets {
		if now.Sub(b.last) > IdleTTL {
			delete(l.buckets, key)
		}
	}
}
