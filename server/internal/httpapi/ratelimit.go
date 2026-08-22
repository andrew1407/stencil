package httpapi

// Abuse guards for the non-LLM write routes: token issuance (per client IP —
// no session exists yet) and project creation / file uploads (per session).
// The bucket is modeled on llmlimit.go's per-session limiter, kept separate so
// LLM spend tuning stays independent. In-process only, like its model.

import (
	"net"
	"net/http"
	"sync"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

// keyLimiter is a per-key token bucket: capacity = the per-minute rate,
// refilled continuously, so a caller may burst up to a minute's worth and
// then settles to the configured pace. nil = unlimited.
type keyLimiter struct {
	mu      sync.Mutex
	perMin  float64
	buckets map[string]*keyBucket
	now     func() time.Time // test seam
}

type keyBucket struct {
	tokens float64
	last   time.Time
}

func newKeyLimiter(perMin int) *keyLimiter {
	if perMin <= 0 {
		return nil // unlimited: no map, no lock, no sweep
	}
	return &keyLimiter{
		perMin:  float64(perMin),
		buckets: make(map[string]*keyBucket),
		now:     time.Now,
	}
}

// allow spends one token for key, reporting whether it was available. A nil
// limiter is unlimited, so callers need no special case.
func (l *keyLimiter) allow(key string) bool {
	if l == nil {
		return true
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	b := l.buckets[key]
	if b == nil {
		b = &keyBucket{tokens: l.perMin, last: now}
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

// sweep drops buckets nobody has touched within idleBucketTTL (llmlimit.go).
// Called under the lock, only when a new key appears.
func (l *keyLimiter) sweep(now time.Time) {
	for key, b := range l.buckets {
		if now.Sub(b.last) > idleBucketTTL {
			delete(l.buckets, key)
		}
	}
}

// limitByIP gates an unauthenticated route by client IP (the only stable key
// before a session exists).
func limitByIP(l *keyLimiter, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		host, _, err := net.SplitHostPort(r.RemoteAddr)
		if err != nil {
			host = r.RemoteAddr
		}
		if !l.allow(host) {
			tooManyRequests(w, "too many token requests")
			return
		}
		next(w, r)
	}
}

// limitBySession gates a write route by session id. It runs inside the auth
// guard, so the session is always on the context.
func limitBySession(l *keyLimiter, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if sess, ok := auth.SessionFromContext(r.Context()); ok && !l.allow(sess.ID) {
			tooManyRequests(w, "write rate exceeded")
			return
		}
		next(w, r)
	}
}

func tooManyRequests(w http.ResponseWriter, msg string) {
	w.Header().Set("Retry-After", "60")
	writeErr(w, http.StatusTooManyRequests, protocol.CodeRateLimited, msg+"; retry later")
}
