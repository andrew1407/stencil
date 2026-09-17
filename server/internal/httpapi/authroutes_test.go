package httpapi

// POST /auth/token: the admin gate, the AUTH_OPEN opt-out, and what a bearer
// does and does not open.

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

func TestAuthGate(t *testing.T) {
	api, _ := testAPI(t, "")
	// No token -> 401 on protected route.
	if rec := do(t, api, http.MethodGet, "/projects", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth GET /projects: code %d", rec.Code)
	}
	tok := issueToken(t, api, "")
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("auth GET /projects: code %d", rec.Code)
	}
}

func TestAdminTokenGatesIssuance(t *testing.T) {
	api, _ := testAPI(t, "secret-admin")
	// Wrong/absent admin token -> 401.
	if rec := do(t, api, http.MethodPost, "/auth/token", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("issuance without admin token should be 401, got %d", rec.Code)
	}
	// Correct admin token -> 200.
	tok := issueToken(t, api, "secret-admin")
	if tok == "" {
		t.Fatal("expected a token")
	}
}

// TestOpenAuthIssuance: with AuthOpen (AUTH_OPEN=1) issuance succeeds with no
// bearer at all; the admin bearer keeps working; everything else stays
// token-gated. The default (AuthOpen unset) stays closed — see
// TestAdminTokenGatesIssuance.
func TestOpenAuthIssuance(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(),
		AdminToken: testAdmin, AuthOpen: true})

	// No bearer at all -> 200 with a real session token.
	rec := do(t, api, http.MethodPost, "/auth/token", "", nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("open issuance without bearer: code %d body %s", rec.Code, rec.Body.String())
	}
	var resp protocol.TokenResponse
	json.Unmarshal(rec.Body.Bytes(), &resp)
	if resp.Token == "" {
		t.Fatal("expected a token")
	}
	if rec := do(t, api, http.MethodGet, "/projects", resp.Token, nil); rec.Code != http.StatusOK {
		t.Fatalf("minted token should authenticate: code %d", rec.Code)
	}
	// An explicit admin bearer still works too.
	if tok := issueToken(t, api, testAdmin); tok == "" {
		t.Fatal("admin bearer should still mint")
	}
	// Protected routes remain token-gated.
	if rec := do(t, api, http.MethodGet, "/projects", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth GET /projects should stay 401, got %d", rec.Code)
	}
}

// TestOpenAuthStillRateLimited: the per-IP issuance limiter applies to open
// issuance too.
func TestOpenAuthStillRateLimited(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(),
		AuthOpen: true, AuthRatePerMin: 2})

	issue := func() *httptest.ResponseRecorder {
		req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
		req.RemoteAddr = "192.0.2.7:1"
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec
	}
	for i := 0; i < 2; i++ {
		if rec := issue(); rec.Code != http.StatusOK {
			t.Fatalf("open issue %d: code %d body %s", i, rec.Code, rec.Body.String())
		}
	}
	if rec := issue(); rec.Code != http.StatusTooManyRequests {
		t.Fatalf("flood should 429 in open mode, got %d", rec.Code)
	}
}

// TestEmptyAdminTokenClosesIssuance pins the fail-closed contract: an API wired
// with NO admin token refuses issuance outright (config.Load generates a per-boot
// token so a real server never runs in this state — but if it does, closed > open).
func TestEmptyAdminTokenClosesIssuance(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc()})
	rec := do(t, api, http.MethodPost, "/auth/token", "", nil)
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("issuance with no admin token configured should 401, got %d", rec.Code)
	}
}

// TestAdminTokenNotUsableAsBearer: the admin token gates issuance but is not itself
// a session — presenting it as a Bearer on a protected route must be rejected (401).
func TestAdminTokenNotUsableAsBearer(t *testing.T) {
	const admin = "secret-admin"
	api, _ := testAPI(t, admin)
	if rec := do(t, api, http.MethodGet, "/projects", admin, nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("admin token used as a session bearer should 401, got %d", rec.Code)
	}
	// A real issued token still works.
	tok := issueToken(t, api, admin)
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("issued token should 200, got %d", rec.Code)
	}
}
