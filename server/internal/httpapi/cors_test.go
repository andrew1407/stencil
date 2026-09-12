package httpapi

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func okHandler() http.Handler {
	return http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK)
		_, _ = rw.Write([]byte("ok"))
	})
}

func TestCORSPreflightShortCircuits(t *testing.T) {
	h := CORS([]string{"*"})(okHandler())
	req := httptest.NewRequest(http.MethodOptions, "/projects", nil)
	req.Header.Set("Origin", "http://localhost:8080")
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)

	if rec.Code != http.StatusNoContent {
		t.Fatalf("preflight status = %d, want 204", rec.Code)
	}
	if got := rec.Header().Get("Access-Control-Allow-Origin"); got != "http://localhost:8080" {
		t.Fatalf("allow-origin = %q, want reflected origin", got)
	}
	if got := rec.Header().Get("Access-Control-Allow-Headers"); got != corsAllowHeaders {
		t.Fatalf("allow-headers = %q, want %q", got, corsAllowHeaders)
	}
	if rec.Body.Len() != 0 {
		t.Fatalf("preflight should have empty body, got %q", rec.Body.String())
	}
}

func TestCORSActualRequestGetsHeaderAndPassesThrough(t *testing.T) {
	h := CORS([]string{"*"})(okHandler())
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req.Header.Set("Origin", "http://example.test")
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK || rec.Body.String() != "ok" {
		t.Fatalf("expected handler to run; got status=%d body=%q", rec.Code, rec.Body.String())
	}
	if got := rec.Header().Get("Access-Control-Allow-Origin"); got != "http://example.test" {
		t.Fatalf("allow-origin = %q, want reflected origin", got)
	}
}

func TestCORSDisallowedOriginGetsNoHeaderButStillServes(t *testing.T) {
	h := CORS([]string{"http://allowed.test"})(okHandler())
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req.Header.Set("Origin", "http://evil.test")
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200 (CORS does not block server-side)", rec.Code)
	}
	if got := rec.Header().Get("Access-Control-Allow-Origin"); got != "" {
		t.Fatalf("allow-origin = %q, want empty for disallowed origin", got)
	}
}

func TestCORSExplicitOriginAllowed(t *testing.T) {
	h := CORS([]string{"http://localhost:8080", "http://localhost:3000"})(okHandler())
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req.Header.Set("Origin", "http://localhost:3000")
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)

	if got := rec.Header().Get("Access-Control-Allow-Origin"); got != "http://localhost:3000" {
		t.Fatalf("allow-origin = %q, want the matched explicit origin", got)
	}
}

func TestCORSEmptyListAllowsLoopbackOnly(t *testing.T) {
	// nil/empty allowlist = the loopback-only default: local dev pages work,
	// everything else gets no ACAO header.
	for _, origins := range [][]string{nil, {}} {
		h := CORS(origins)(okHandler())
		cases := []struct {
			origin string
			want   string
		}{
			{"http://localhost:8080", "http://localhost:8080"},
			{"http://127.0.0.1:3000", "http://127.0.0.1:3000"},
			{"https://evil.example", ""},
			{"chrome-extension://abcdefghijklmnop", ""}, // non-http scheme
		}
		for _, c := range cases {
			req := httptest.NewRequest(http.MethodGet, "/projects", nil)
			req.Header.Set("Origin", c.origin)
			rec := httptest.NewRecorder()
			h.ServeHTTP(rec, req)
			if got := rec.Header().Get("Access-Control-Allow-Origin"); got != c.want {
				t.Fatalf("origin %q: allow-origin = %q, want %q", c.origin, got, c.want)
			}
		}
	}
}

func TestCORSNoOriginHeaderUntouched(t *testing.T) {
	h := CORS([]string{"*"})(okHandler())
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)

	if got := rec.Header().Get("Access-Control-Allow-Origin"); got != "" {
		t.Fatalf("allow-origin = %q, want empty when no Origin sent", got)
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
}
