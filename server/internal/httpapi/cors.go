package httpapi

import (
	"net/http"
	"strings"
)

// corsHeaders the browser clients (browser/ app and the extension) need to call
// the REST API cross-origin. Authentication is a bearer token (not cookies), so
// credentialed CORS is not required and "*" is a safe default.
const (
	corsAllowMethods = "GET, POST, PUT, DELETE, OPTIONS"
	corsAllowHeaders = "Authorization, Content-Type, X-Admin-Token"
	corsMaxAge       = "600"
)

// CORS wraps a handler with cross-origin support for the given allowed origins.
// A list containing "*" (the default) reflects any Origin. Preflight OPTIONS
// requests are answered here with 204 before they reach the method-pattern mux
// (which would otherwise 405 them). Requests without an Origin header, and those
// from a disallowed origin, pass through untouched.
func CORS(origins []string) func(http.Handler) http.Handler {
	allowAny := false
	allowed := map[string]struct{}{}
	for _, o := range origins {
		if o == "*" {
			allowAny = true
		}
		allowed[o] = struct{}{}
	}

	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			origin := r.Header.Get("Origin")
			if origin != "" && originAllowed(origin, allowAny, allowed) {
				h := w.Header()
				h.Set("Access-Control-Allow-Origin", origin)
				h.Add("Vary", "Origin")
				h.Set("Access-Control-Allow-Methods", corsAllowMethods)
				h.Set("Access-Control-Allow-Headers", corsAllowHeaders)
				h.Set("Access-Control-Max-Age", corsMaxAge)
			}
			// Answer preflight here so OPTIONS never falls through to the mux.
			if r.Method == http.MethodOptions {
				w.WriteHeader(http.StatusNoContent)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}

func originAllowed(origin string, allowAny bool, allowed map[string]struct{}) bool {
	if allowAny {
		return true
	}
	_, ok := allowed[strings.TrimRight(origin, "/")]
	if ok {
		return true
	}
	_, ok = allowed[origin]
	return ok
}
