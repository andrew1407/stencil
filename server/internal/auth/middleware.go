package auth

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"

	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
)

type ctxKey int

const sessionKey ctxKey = 0

// Middleware gates a handler behind bearer-token auth. On success the resolved
// Session is attached to the request context (see SessionFromContext). On
// failure it writes a 401 JSON error and does not call next.
func Middleware(resolver SessionResolver) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
			token := BearerToken(req)
			sess, err := Verify(req.Context(), resolver, token, clock.NowMs())
			if err != nil {
				writeUnauthorized(rw)
				return
			}
			ctx := context.WithValue(req.Context(), sessionKey, sess)
			next.ServeHTTP(rw, req.WithContext(ctx))
		})
	}
}

// BearerToken extracts the token from the Authorization header, tolerating a
// "Bearer " prefix in any case. Only WebSocket upgrade requests may fall back
// to a `token` query param (the browser WebSocket API cannot set headers);
// on plain REST a URL token is ignored — it would leak via logs and referrers.
func BearerToken(req *http.Request) string {
	h := req.Header.Get("Authorization")
	if h != "" {
		if len(h) >= 7 && strings.EqualFold(h[:7], "bearer ") {
			return strings.TrimSpace(h[7:])
		}
		return strings.TrimSpace(h)
	}
	if isWebSocketUpgrade(req) {
		return req.URL.Query().Get("token")
	}
	return ""
}

// isWebSocketUpgrade reports whether r is an RFC6455 upgrade handshake.
func isWebSocketUpgrade(req *http.Request) bool {
	if !strings.EqualFold(req.Header.Get("Upgrade"), "websocket") {
		return false
	}
	for _, tok := range strings.Split(req.Header.Get("Connection"), ",") {
		if strings.EqualFold(strings.TrimSpace(tok), "upgrade") {
			return true
		}
	}
	return false
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
