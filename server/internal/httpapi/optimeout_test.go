package httpapi

// Handlers used to hand r.Context() straight to the store, so a query that never
// answered held a pool connection until the server's 5-minute write timeout.

import (
	"context"
	"net/http"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// stalledStore answers no store call until its context is done, standing in for
// a query stuck behind a lock or an unresponsive database.
type stalledStore struct {
	*testutil.MemStore
}

func (s *stalledStore) block(ctx context.Context) error {
	<-ctx.Done()
	return ctx.Err()
}

func (s *stalledStore) ListProjects(ctx context.Context, _ store.ProjectPage) ([]protocol.ProjectRecord, error) {
	return nil, s.block(ctx)
}

func (s *stalledStore) GetProject(ctx context.Context, _ string) (protocol.ProjectRecord, error) {
	return protocol.ProjectRecord{}, s.block(ctx)
}

func stalledAPI(t *testing.T, timeout time.Duration) (*API, string) {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	mem := testutil.NewMemStore()
	api := New(Deps{Projects: &stalledStore{MemStore: mem}, Sessions: mem, Files: fs,
		Bus: bus.NewInProc(), AdminToken: testAdmin, OpTimeout: timeout})
	return api, issueToken(t, api, "")
}

// A store that never answers must not pin the handler: the operation deadline
// fires and the route reports the failure instead of hanging.
func TestSlowStoreCallsHitTheOperationTimeout(t *testing.T) {
	api, tok := stalledAPI(t, 50*time.Millisecond)
	for _, path := range []string{"/projects", "/projects/p1", "/projects/p1/files/original"} {
		done := make(chan int, 1)
		go func() { done <- do(t, api, http.MethodGet, path, tok, nil).Code }()
		select {
		case code := <-done:
			if code != http.StatusInternalServerError {
				t.Errorf("%s: code %d, want 500 after the deadline", path, code)
			}
		case <-time.After(5 * time.Second):
			t.Fatalf("%s: the handler is still waiting on the store", path)
		}
	}
}

// The default applies when nothing is configured, so a caller that forgets the
// field is still bounded — and it is not so tight that a normal query trips it.
func TestOpTimeoutDefaultsAndIsAppliedToRequests(t *testing.T) {
	api, _ := testAPI(t, "")
	if api.deps.OpTimeout != defaultOpTimeout {
		t.Fatalf("OpTimeout default = %v, want %v", api.deps.OpTimeout, defaultOpTimeout)
	}
	// And an ordinary request is untouched by it.
	tok := issueToken(t, api, "")
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("a fast store should answer normally, got %d", rec.Code)
	}
}
