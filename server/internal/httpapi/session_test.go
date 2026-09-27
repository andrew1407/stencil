package httpapi

// GET /auth/session: the cheap token probe that replaces listing every project.

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/testutil"
)

// The probe's three answers, pinned as text: an issued token, no bearer, and the admin token — which
// is no session, so clients that tell the two apart by a 401 keep minting with it.
func TestSessionProbeGolden(t *testing.T) {
	t.Cleanup(clock.Stub(clock.Fixed(1_000)))
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	var b strings.Builder
	for _, tc := range []struct{ name, bearer string }{
		{"issued token", tok}, {"no bearer", ""}, {"the admin token", testAdmin},
	} {
		rec := do(t, api, http.MethodGet, "/auth/session", tc.bearer, nil)
		fmt.Fprintf(&b, "== GET /auth/session — %s\n%d %s\n%s\n",
			tc.name, rec.Code, rec.Header().Get("Content-Type"), rec.Body.String())
	}
	checkGolden(t, "auth_session.txt", b.String())
}

// An expired session fails the probe like any other route.
func TestSessionProbeRefusesAnExpiredToken(t *testing.T) {
	api, st := testAPI(t, "")
	if _, err := st.CreateSession(context.Background(), auth.HashToken("old"), "", 1, 2); err != nil {
		t.Fatal(err)
	}
	if rec := do(t, api, http.MethodGet, "/auth/session", "old", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("expired token: code %d, want 401", rec.Code)
	}
}

// stalledResolver never answers a token lookup until its context ends.
type stalledResolver struct{ *testutil.MemStore }

func (stalledResolver) ResolveToken(ctx context.Context, _ []byte) (auth.Session, error) {
	<-ctx.Done()
	return auth.Session{}, ctx.Err()
}

// The guard's lookup runs under OP_TIMEOUT_SECONDS: a stuck one refuses the request instead of pinning it.
func TestAuthLookupHitsTheOperationTimeout(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	mem := testutil.NewMemStore()
	api := New(Deps{Projects: mem, Sessions: stalledResolver{mem}, Files: fs, Bus: eventbus.NewInProc(),
		AdminToken: testAdmin, OpTimeout: 50 * time.Millisecond})
	done := make(chan int, 1)
	go func() { done <- do(t, api, http.MethodGet, "/auth/session", "tok", nil).Code }()
	select {
	case code := <-done:
		if code != http.StatusUnauthorized {
			t.Fatalf("code %d, want 401 once the lookup's deadline passed", code)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("the guard is still waiting on the session store")
	}
}
