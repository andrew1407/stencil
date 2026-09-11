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
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			token := BearerToken(r)
			sess, err := Verify(r.Context(), resolver, token, clock.NowMs())
			if err != nil {
				writeUnauthorized(w)
				return
			}
			ctx := context.WithValue(r.Context(), sessionKey, sess)
			next.ServeHTTP(w, r.WithContext(ctx))
		})
	}
}

// BearerToken extracts the token from the Authorization header, tolerating a
// "Bearer " prefix in any case. Only WebSocket upgrade requests may fall back
// to a `token` query param (the browser WebSocket API cannot set headers);
// on plain REST a URL token is ignored — it would leak via logs and referrers.
func BearerToken(r *http.Request) string {
	h := r.Header.Get("Authorization")
	if h != "" {
		if len(h) >= 7 && strings.EqualFold(h[:7], "bearer ") {
			return strings.TrimSpace(h[7:])
		}
		return strings.TrimSpace(h)
	}
	if isWebSocketUpgrade(r) {
		return r.URL.Query().Get("token")
	}
	return ""
}

// isWebSocketUpgrade reports whether r is an RFC6455 upgrade handshake.
func isWebSocketUpgrade(r *http.Request) bool {
	if !strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		return false
	}
	for _, tok := range strings.Split(r.Header.Get("Connection"), ",") {
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

func writeUnauthorized(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("WWW-Authenticate", "Bearer")
	w.WriteHeader(http.StatusUnauthorized)
	_ = json.NewEncoder(w).Encode(protocol.ErrorResponse{
		Code:    protocol.CodeUnauthorized,
		Message: "missing or invalid token",
	})
}
