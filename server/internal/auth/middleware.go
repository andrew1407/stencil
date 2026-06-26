package auth

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"time"

	"stencil/server/internal/protocol"
)

type ctxKey int

const sessionKey ctxKey = 0

// nowMs is overridable in tests; production uses the wall clock.
var nowMs = func() int64 { return time.Now().UnixMilli() }

// Middleware gates a handler behind bearer-token auth. On success the resolved
// Session is attached to the request context (see SessionFromContext). On
// failure it writes a 401 JSON error and does not call next.
func Middleware(resolver SessionResolver) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			token := BearerToken(r)
			sess, err := Verify(r.Context(), resolver, token, nowMs())
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
// "Bearer " prefix in any case. As a fallback (used by WebSocket clients that
// cannot set headers before the upgrade) it also accepts a `token` query param.
func BearerToken(r *http.Request) string {
	h := r.Header.Get("Authorization")
	if h != "" {
		if len(h) >= 7 && strings.EqualFold(h[:7], "bearer ") {
			return strings.TrimSpace(h[7:])
		}
		return strings.TrimSpace(h)
	}
	return r.URL.Query().Get("token")
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
