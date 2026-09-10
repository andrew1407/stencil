package httpapi

// Abuse guards for the non-LLM write routes: token issuance (per client IP —
// no session exists yet) and project creation / file uploads (per session).
// Both spend the shared in-process bucket in internal/ratelimit, as /llm/chat
// does (llmlimit.go).

import (
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/ratelimit"
)

// clientIP is the key a per-IP limiter spends against: the peer, or the
// forwarded client when the peer is in TRUSTED_PROXY_CIDRS — without which
// every request behind a TLS-terminating proxy shares the proxy's one bucket.
func (a *API) clientIP(r *http.Request) string {
	return ratelimit.ClientIP(r.RemoteAddr, r.Header.Get("X-Forwarded-For"), a.deps.TrustedProxies)
}

// limitByIP gates an unauthenticated route by client IP (the only stable key
// before a session exists).
func (a *API) limitByIP(l *ratelimit.Limiter, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !l.Allow(a.clientIP(r)) {
			tooManyRequests(w, "too many token requests")
			return
		}
		next(w, r)
	}
}

// limitBySession gates a write route by session id. It runs inside the auth
// guard, so the session is always on the context.
func limitBySession(l *ratelimit.Limiter, next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if sess, ok := auth.SessionFromContext(r.Context()); ok && !l.Allow(sess.ID) {
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
