package httpapi

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// A token-issuance flood from one IP hits 429 once AUTH_RATE_PER_MINUTE is
// spent; a different IP still gets through (the key is the client address).
func TestAuthTokenFloodIsRateLimitedPerIP(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: testAdmin, AuthRatePerMin: 3})

	issue := func(remote string) *httptest.ResponseRecorder {
		req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
		req.Header.Set("X-Admin-Token", testAdmin)
		req.RemoteAddr = remote
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec
	}

	for i := 0; i < 3; i++ {
		if rec := issue("192.0.2.1:1234"); rec.Code != http.StatusOK {
			t.Fatalf("issue %d: code %d body %s", i, rec.Code, rec.Body.String())
		}
	}
	rec := issue("192.0.2.1:9999") // same IP, different port: same bucket
	if rec.Code != http.StatusTooManyRequests {
		t.Fatalf("flood should 429, got %d", rec.Code)
	}
	if rec.Header().Get("Retry-After") == "" {
		t.Error("429 should carry Retry-After")
	}
	var resp protocol.ErrorResponse
	if err := jsonDecode(rec, &resp); err != nil || resp.Code != protocol.CodeRateLimited {
		t.Fatalf("want code %q, got %+v (err %v)", protocol.CodeRateLimited, resp, err)
	}
	// A different client is on its own bucket.
	if rec := issue("203.0.113.9:1"); rec.Code != http.StatusOK {
		t.Fatalf("other IP should still issue, got %d", rec.Code)
	}
}

// Project creation and file uploads share the per-session write bucket; reads
// are never limited, and a second session has its own budget.
func TestWriteRateLimitIsPerSession(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: testAdmin, WriteRatePerMin: 2})
	tok := issueToken(t, api, "")

	body := []byte(`{"name":"P","source":"http://x/a.png","hasImage":true}`)
	if rec := do(t, api, http.MethodPost, "/projects", tok, body); rec.Code != http.StatusCreated {
		t.Fatalf("create 1: code %d", rec.Code)
	}
	if rec := do(t, api, http.MethodPost, "/projects", tok, body); rec.Code != http.StatusCreated {
		t.Fatalf("create 2: code %d", rec.Code)
	}
	rec := do(t, api, http.MethodPost, "/projects", tok, body)
	if rec.Code != http.StatusTooManyRequests {
		t.Fatalf("create 3 should 429, got %d", rec.Code)
	}
	// The same empty bucket also refuses an upload...
	if rec := do(t, api, http.MethodPost, "/projects/p/files/original?ext=png", tok, []byte("x")); rec.Code != http.StatusTooManyRequests {
		t.Fatalf("upload over the write budget should 429, got %d", rec.Code)
	}
	// ...but reads are untouched.
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("reads must not be rate limited, got %d", rec.Code)
	}
	// A fresh session gets its own budget.
	tok2 := issueToken(t, api, "")
	if rec := do(t, api, http.MethodPost, "/projects", tok2, body); rec.Code != http.StatusCreated {
		t.Fatalf("other session should create, got %d", rec.Code)
	}
}

// An upload that would push the filestore past STORAGE_QUOTA_BYTES is refused
// with 507; one that fits is stored.
func TestUploadAgainstStorageQuota(t *testing.T) {
	fs, err := filestore.NewWithQuota(t.TempDir(), 1024)
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Q","source":"s","hasImage":true}`))
	if rec.Code != http.StatusCreated {
		t.Fatalf("create: code %d", rec.Code)
	}
	var created protocol.ProjectRecord
	if err := jsonDecode(rec, &created); err != nil {
		t.Fatal(err)
	}

	under := make([]byte, 600)
	if rec := do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/original?ext=png&w=1&h=1", tok, under); rec.Code != http.StatusCreated {
		t.Fatalf("under-quota upload: code %d body %s", rec.Code, rec.Body.String())
	}
	over := make([]byte, 600) // 600 + 600 > 1024
	rec = do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/result?ext=png", tok, over)
	if rec.Code != http.StatusInsufficientStorage {
		t.Fatalf("over-quota upload should 507, got %d body %s", rec.Code, rec.Body.String())
	}
	// Replacing the original re-uses its budget, so an equal-size re-upload fits.
	if rec := do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/original?ext=png&w=1&h=1", tok, under); rec.Code != http.StatusCreated {
		t.Fatalf("same-size replacement should fit, got %d", rec.Code)
	}
}

// jsonDecode unmarshals a recorder body (tiny local helper).
func jsonDecode(rec *httptest.ResponseRecorder, v any) error {
	return json.Unmarshal(rec.Body.Bytes(), v)
}

// issuer builds an API whose /auth/token bucket allows one attempt per key, and
// returns a func that issues as one (peer, X-Forwarded-For) pair.
func issuer(t *testing.T, trusted ...netip.Prefix) func(peer, xff string) int {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: testAdmin, AuthRatePerMin: 1, TrustedProxies: trusted})
	return func(peer, xff string) int {
		req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
		req.Header.Set("X-Admin-Token", testAdmin)
		req.RemoteAddr = peer
		if xff != "" {
			req.Header.Set("X-Forwarded-For", xff)
		}
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec.Code
	}
}

// X-Forwarded-For only moves the limiter key when the PEER is a trusted proxy.
// From anyone else the header is ignored, so it cannot buy a fresh bucket.
func TestPerIPKeyHonoursForwardedForOnlyFromTrustedPeers(t *testing.T) {
	loopback := netip.MustParsePrefix("127.0.0.0/8")
	cases := []struct {
		name          string
		trusted       []netip.Prefix
		first, second [2]string // {peer, X-Forwarded-For}
		want          int
	}{
		{"spoofed header from an untrusted peer shares one bucket", nil,
			[2]string{"203.0.113.5:1", "198.51.100.1"}, [2]string{"203.0.113.5:2", "198.51.100.2"},
			http.StatusTooManyRequests},
		{"no trusted CIDRs: the header is ignored entirely", nil,
			[2]string{"127.0.0.1:1", "198.51.100.1"}, [2]string{"127.0.0.1:2", "198.51.100.2"},
			http.StatusTooManyRequests},
		{"behind a trusted proxy each forwarded client gets its own bucket", []netip.Prefix{loopback},
			[2]string{"127.0.0.1:1", "198.51.100.1"}, [2]string{"127.0.0.1:2", "198.51.100.2"},
			http.StatusOK},
		{"trusted proxy, same forwarded client twice", []netip.Prefix{loopback},
			[2]string{"127.0.0.1:1", "198.51.100.1"}, [2]string{"127.0.0.1:2", "198.51.100.1"},
			http.StatusTooManyRequests},
		{"trusted proxy, spoofed hop appended by the client", []netip.Prefix{loopback},
			[2]string{"127.0.0.1:1", "198.51.100.1"}, [2]string{"127.0.0.1:2", "10.9.9.9, 198.51.100.1"},
			http.StatusTooManyRequests},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			issue := issuer(t, tc.trusted...)
			if code := issue(tc.first[0], tc.first[1]); code != http.StatusOK {
				t.Fatalf("first issue: code %d", code)
			}
			if code := issue(tc.second[0], tc.second[1]); code != tc.want {
				t.Fatalf("second issue: code %d, want %d", code, tc.want)
			}
		})
	}
}
