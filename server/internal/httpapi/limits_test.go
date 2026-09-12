package httpapi

// Every abuse guard on the request path in one place: the per-IP token bucket,
// the per-session write and /llm/chat buckets, and the /llm/chat in-flight gate.
// The bucket arithmetic itself is tested once, in internal/ratelimit.

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"sync"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// limitAPI builds an API from the limiter fields a test cares about, filling in
// the stores, bus and admin token.
func limitAPI(t *testing.T, d Deps) *API {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	d.Projects, d.Sessions, d.Files, d.Bus = st, st, fs, bus.NewInProc()
	if d.AdminToken == "" {
		d.AdminToken = testAdmin
	}
	return New(d)
}

// jsonDecode unmarshals a recorder body (shared by the route suites).
func jsonDecode(rec *httptest.ResponseRecorder, v any) error {
	return json.Unmarshal(rec.Body.Bytes(), v)
}

// want429 asserts the shared shape of a refusal: the status, the Retry-After a
// client needs to back off, and the machine-readable code.
func want429(t *testing.T, rec *httptest.ResponseRecorder, what string) {
	t.Helper()
	if rec.Code != http.StatusTooManyRequests {
		t.Fatalf("%s: code %d, want 429 (body %s)", what, rec.Code, rec.Body.String())
	}
	if rec.Header().Get("Retry-After") == "" {
		t.Errorf("%s: a 429 should say when to come back", what)
	}
	var resp protocol.ErrorResponse
	if err := jsonDecode(rec, &resp); err != nil || resp.Code != protocol.CodeRateLimited {
		t.Errorf("%s: want code %q, got %+v (err %v)", what, protocol.CodeRateLimited, resp, err)
	}
}

// issuer returns a func that posts /auth/token as one (peer, X-Forwarded-For)
// pair against a bucket of `perMin` attempts.
func issuer(t *testing.T, perMin int, trusted ...netip.Prefix) func(peer, xff string) *httptest.ResponseRecorder {
	t.Helper()
	api := limitAPI(t, Deps{AuthRatePerMin: perMin, TrustedProxies: trusted})
	return func(peer, xff string) *httptest.ResponseRecorder {
		req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
		req.Header.Set("X-Admin-Token", testAdmin)
		req.RemoteAddr = peer
		if xff != "" {
			req.Header.Set("X-Forwarded-For", xff)
		}
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec
	}
}

// A token-issuance flood from one IP hits 429 once AUTH_RATE_PER_MINUTE is
// spent; a different IP still gets through (the key is the client address).
func TestAuthTokenFloodIsRateLimitedPerIP(t *testing.T) {
	issue := issuer(t, 3)
	for i := 0; i < 3; i++ {
		if rec := issue("192.0.2.1:1234", ""); rec.Code != http.StatusOK {
			t.Fatalf("issue %d: code %d body %s", i, rec.Code, rec.Body.String())
		}
	}
	want429(t, issue("192.0.2.1:9999", ""), "same IP, different port: same bucket")
	if rec := issue("203.0.113.9:1", ""); rec.Code != http.StatusOK {
		t.Fatalf("other IP should still issue, got %d", rec.Code)
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
			issue := issuer(t, 1, tc.trusted...)
			if rec := issue(tc.first[0], tc.first[1]); rec.Code != http.StatusOK {
				t.Fatalf("first issue: code %d", rec.Code)
			}
			if rec := issue(tc.second[0], tc.second[1]); rec.Code != tc.want {
				t.Fatalf("second issue: code %d, want %d", rec.Code, tc.want)
			}
		})
	}
}

// Project creation and file uploads share the per-session write bucket; reads
// are never limited, and a second session has its own budget.
func TestWriteRateLimitIsPerSession(t *testing.T) {
	api := limitAPI(t, Deps{WriteRatePerMin: 2})
	tok := issueToken(t, api, "")
	body := []byte(`{"name":"P","source":"http://x/a.png","hasImage":true}`)

	for i := 0; i < 2; i++ {
		if rec := do(t, api, http.MethodPost, "/projects", tok, body); rec.Code != http.StatusCreated {
			t.Fatalf("create %d: code %d", i+1, rec.Code)
		}
	}
	want429(t, do(t, api, http.MethodPost, "/projects", tok, body), "create over the budget")
	// The same empty bucket also refuses an upload...
	want429(t, do(t, api, http.MethodPost, "/projects/p/files/original?ext=png", tok, []byte("x")), "upload")
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

// A session token is valid for days and every accepted turn spends the
// operator's upstream, so /llm/chat has its own per-session bucket.
func TestLLMChatRateLimitReturns429(t *testing.T) {
	api := limitAPI(t, Deps{LLM: &testutil.EchoLLM{}, LLMRatePerMin: 2})
	tok := issueToken(t, api, "")
	body := []byte(`{"messages":[{"role":"user","text":"hi"}]}`)

	for i := 0; i < 2; i++ {
		if rec := do(t, api, http.MethodPost, "/llm/chat", tok, body); rec.Code != http.StatusOK {
			t.Fatalf("turn %d: code %d", i+1, rec.Code)
		}
	}
	want429(t, do(t, api, http.MethodPost, "/llm/chat", tok, body), "turn over the rate")
	// GET /llm/info is not metered — it spends nothing upstream.
	if rec := do(t, api, http.MethodGet, "/llm/info", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("info should stay reachable while chat is limited, got %d", rec.Code)
	}
}

func TestGateCapsConcurrencyAndReleases(t *testing.T) {
	g := newLLMGate(2)
	r1, ok1 := g.enter()
	_, ok2 := g.enter()
	if !ok1 || !ok2 {
		t.Fatal("both slots should be available")
	}
	if _, ok := g.enter(); ok {
		t.Fatal("a third concurrent call must be refused, not queued")
	}
	r1() // first call finishes
	r3, ok := g.enter()
	if !ok {
		t.Fatal("the freed slot should be reusable")
	}
	r3()

	// 0 = unlimited, and the release func is always safe to call.
	unl := newLLMGate(0)
	for i := 0; i < 100; i++ {
		rel, ok := unl.enter()
		if !ok {
			t.Fatal("an unlimited gate must never refuse")
		}
		rel()
	}
}

// blockingLLM holds each call until released, so in-flight calls really overlap.
type blockingLLM struct {
	release chan struct{}
	wg      sync.WaitGroup
}

func (b *blockingLLM) Chat(_ context.Context, _ protocol.LlmChatRequest) (protocol.LlmChatResponse, error) {
	b.wg.Done()
	<-b.release
	return protocol.LlmChatResponse{Model: "m", Text: "ok", StopReason: "end_turn"}, nil
}
func (b *blockingLLM) Model() string { return "m" }

func TestLLMChatRefusesOverTheInFlightCap(t *testing.T) {
	blocker := &blockingLLM{release: make(chan struct{})}
	api := limitAPI(t, Deps{LLM: blocker, LLMMaxInFlight: 1})
	tok := issueToken(t, api, "")
	body := []byte(`{"messages":[{"role":"user","text":"hi"}]}`)

	blocker.wg.Add(1)
	go func() { do(t, api, http.MethodPost, "/llm/chat", tok, body) }()
	blocker.wg.Wait() // the first call is now inside Chat, holding the only slot

	want429(t, do(t, api, http.MethodPost, "/llm/chat", tok, body), "turn over the in-flight cap")
	close(blocker.release)
}
