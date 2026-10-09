package auth

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"time"

	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
)

type ctxKey int

const sessionKey ctxKey = 0

// Middleware gates a handler behind bearer-token auth, attaching the Session or answering 401. Its store
// lookup runs on every request, so under lookupTimeout (OP_TIMEOUT_SECONDS; 0 = the request's own).
func Middleware(resolver SessionResolver, lookupTimeout time.Duration) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
			token := BearerToken(req)
			ctx, cancel := clock.WithTimeout(req.Context(), lookupTimeout)
			sess, err := Verify(ctx, resolver, token, clock.NowMs())
			cancel()
			if err != nil {
				writeUnauthorized(rw)
				return
			}
			next.ServeHTTP(rw, req.WithContext(context.WithValue(req.Context(), sessionKey, sess)))
		})
	}
}

// WSRoute is the one path a WebSocket upgrade is served on; the hello frame carries its token.
const WSRoute = "/ws"

// BearerToken extracts the token from Authorization, tolerating any case of "Bearer ". A URL token is
// never read: it would leak via logs.
func BearerToken(req *http.Request) string {
	h := req.Header.Get("Authorization")
	if len(h) >= 7 && strings.EqualFold(h[:7], "bearer ") {
		return strings.TrimSpace(h[7:])
	}
	return strings.TrimSpace(h)
}

// SessionFromContext returns the authenticated session attached by Middleware.
func SessionFromContext(ctx context.Context) (Session, bool) {
	sess, ok := ctx.Value(sessionKey).(Session)
	return sess, ok
}

func writeUnauthorized(rw http.ResponseWriter) {
	rw.Header().Set("Content-Type", "application/json")
	rw.Header().Set("WWW-Authenticate", "Bearer")
	rw.WriteHeader(http.StatusUnauthorized)
	_ = json.NewEncoder(rw).Encode(protocol.ErrorResponse{
		Code:    protocol.CodeUnauthorized,
		Message: "missing or invalid token",
	})
}
