package httpapi

// Abuse guards for the non-LLM write routes: token issuance (per client IP —
// no session exists yet) and every project or file write (per session).
// Both spend the shared in-process bucket in internal/ratelimit, as /llm/chat
// does (llmlimit.go).

import (
	"net/http"
	"strconv"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/ratelimit"
)

// clientIP is the key a per-IP limiter spends against: the peer, or the forwarded client when the peer
// is in TRUSTED_PROXY_CIDRS — without which everything behind one proxy shares the proxy's bucket.
func (a *API) clientIP(req *http.Request) string {
	return ratelimit.ClientIP(req.RemoteAddr, req.Header.Get("X-Forwarded-For"), a.deps.TrustedProxies)
}

// limitByIP gates an unauthenticated route by client IP (the only stable key
// before a session exists).
func (a *API) limitByIP(l *ratelimit.Limiter, next http.HandlerFunc) http.HandlerFunc {
	return func(rw http.ResponseWriter, req *http.Request) {
		if !l.Allow(a.clientIP(req)) {
			a.tooManyRequests(rw, msgTooManyTokenRequests)
			return
		}
		next(rw, req)
	}
}

// limitBySession gates a write route by session id. It runs inside the auth
// guard, so the session is always on the context.
func (a *API) limitBySession(l *ratelimit.Limiter, next http.HandlerFunc) http.HandlerFunc {
	return func(rw http.ResponseWriter, req *http.Request) {
		if sess, ok := auth.SessionFromContext(req.Context()); ok && !l.Allow(sess.ID) {
			a.tooManyRequests(rw, msgWriteRateExceeded)
			return
		}
		next(rw, req)
	}
}

func (a *API) tooManyRequests(rw http.ResponseWriter, msg string) {
	setRetryAfter(rw, a.deps.RetryAfter)
	writeErr(rw, http.StatusTooManyRequests, protocol.CodeRateLimited, msg+msgRetryLater)
}

// setRetryAfter writes the header in whole seconds, never below one.
func setRetryAfter(rw http.ResponseWriter, d time.Duration) {
	rw.Header().Set("Retry-After", strconv.Itoa(max(1, int(d/time.Second))))
}
