package httpapi

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// The bucket must allow a burst up to the per-minute rate, refuse the next
// spend, refill with elapsed time, and keep keys independent. A nil limiter
// (rate 0) is unlimited.
func TestKeyLimiterBuckets(t *testing.T) {
	now := time.Unix(1000, 0)
	l := newKeyLimiter(3)
	l.now = func() time.Time { return now }

	steps := []struct {
		key     string
		advance time.Duration
		want    bool
	}{
		{"a", 0, true}, {"a", 0, true}, {"a", 0, true}, // burst up to capacity
		{"a", 0, false},                // bucket empty
		{"b", 0, true},                 // an independent key is unaffected
		{"a", 20 * time.Second, true},  // 20s at 3/min refills one token
		{"a", 0, false},                // and only one
		{"a", 10 * time.Minute, true},  // refill caps at one minute's worth
		{"a", 0, true}, {"a", 0, true}, // ...i.e. capacity 3
		{"a", 0, false},
	}
	for i, s := range steps {
		now = now.Add(s.advance)
		if got := l.allow(s.key); got != s.want {
			t.Fatalf("step %d (%s): allow=%v, want %v", i, s.key, got, s.want)
		}
	}

	var unlimited *keyLimiter // rate 0 -> nil
	if unlimited = newKeyLimiter(0); unlimited != nil {
		t.Fatal("rate 0 should yield a nil (unlimited) limiter")
	}
	for i := 0; i < 100; i++ {
		if !unlimited.allow("any") {
			t.Fatal("a nil limiter must always allow")
		}
	}
}

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
