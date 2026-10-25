package httpapi

import (
	"net/http"
	"net/netip"
	"net/url"
	"strings"
)

// Authentication is a bearer token, not cookies, so credentialed CORS is never needed. The default
// allows only loopback origins; anything wider — including "*" — must be configured.
const (
	corsAllowMethods  = "GET, POST, PUT, DELETE, OPTIONS"
	corsAllowHeaders  = "Authorization, Content-Type, X-Admin-Token, If-None-Match"
	corsExposeHeaders = "ETag, Retry-After"
	corsMaxAge        = "600"
)

// CORS wraps a handler for the given allowed origins: empty allows loopback only, "*" reflects any
// Origin. Preflight OPTIONS are answered here with 204 before the method-pattern mux would 405 them.
func CORS(origins []string) func(http.Handler) http.Handler {
	allowedOrigin := originRule(origins)
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
			origin := req.Header.Get("Origin")
			if origin != "" && allowedOrigin(origin) {
				h := rw.Header()
				h.Set("Access-Control-Allow-Origin", origin)
				h.Add("Vary", "Origin")
				h.Set("Access-Control-Allow-Methods", corsAllowMethods)
				h.Set("Access-Control-Allow-Headers", corsAllowHeaders)
				h.Set("Access-Control-Expose-Headers", corsExposeHeaders)
				h.Set("Access-Control-Max-Age", corsMaxAge)
			}
			// Answer preflight here so OPTIONS never falls through to the mux.
			if req.Method == http.MethodOptions {
				rw.WriteHeader(http.StatusNoContent)
				return
			}
			next.ServeHTTP(rw, req)
		})
	}
}

func originRule(origins []string) func(string) bool {
	allowAny := false
	allowed := map[string]struct{}{}
	for _, o := range origins {
		if o == "*" {
			allowAny = true
		}
		allowed[o] = struct{}{}
	}
	return func(origin string) bool { return originAllowed(origin, allowAny, allowed) }
}

// extensionSchemes are the browser-extension origins a live feed may come from: an extension's own
// pages cannot be a hostile web page.
var extensionSchemes = []string{"chrome-extension://", "moz-extension://", "safari-web-extension://"}

// FeedOriginAllowed is the live feed's Origin rule (CORS_ORIGINS, loopback by default, plus the
// browser-extension schemes): a hostile page cannot hold sockets open or spend hello failures from the
// victim's address. A request with no Origin — a native client — is not a page and passes.
func FeedOriginAllowed(origins []string) func(string) bool {
	allowed := originRule(origins)
	return func(origin string) bool {
		if origin == "" || allowed(origin) {
			return true
		}
		for _, s := range extensionSchemes {
			if strings.HasPrefix(strings.ToLower(origin), s) {
				return true
			}
		}
		return false
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
	if _, ok = allowed[origin]; ok {
		return true
	}
	return len(allowed) == 0 && isLoopbackOrigin(origin)
}

// isLoopbackOrigin reports whether origin is an http(s) page served from this machine (localhost,
// 127.0.0.0/8, ::1) — the only origins the default, allowlist-free configuration answers.
func isLoopbackOrigin(origin string) bool {
	u, err := url.Parse(origin)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") {
		return false
	}
	host := u.Hostname()
	if strings.EqualFold(host, "localhost") {
		return true
	}
	// A parsed address, never a prefix: "127.evil.example" is a name, not 127.0.0.0/8.
	addr, err := netip.ParseAddr(host)
	return err == nil && addr.Unmap().IsLoopback()
}
