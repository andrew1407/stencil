package httpapi

// The upload's body streams under the request's context; only the store calls take the op timeout, each
// its own, so an upload slower than one timeout is still recorded once its bytes land.

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// deadlineStore refuses a SetFile whose context already ended, as Postgres would.
type deadlineStore struct{ *testutil.MemStore }

func (d deadlineStore) SetFile(ctx context.Context, id string, f store.StoredFile) (protocol.ProjectRecord, error) {
	if err := ctx.Err(); err != nil {
		return protocol.ProjectRecord{}, err
	}
	return d.MemStore.SetFile(ctx, id, f)
}

type pausedBody struct {
	io.Reader
	pause time.Duration
}

func (p *pausedBody) Read(b []byte) (int, error) {
	if p.pause > 0 {
		time.Sleep(p.pause)
		p.pause = 0
	}
	return p.Reader.Read(b)
}

func TestAnUploadSlowerThanTheOpTimeoutIsRecorded(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	mem := testutil.NewMemStore()
	mem.Seed(protocol.ProjectRecord{ID: "p_slow_a"})
	api := New(Deps{Projects: deadlineStore{mem}, Sessions: mem, Files: fs, Bus: eventbus.NewInProc(),
		AdminToken: testAdmin, OpTimeout: 40 * time.Millisecond})
	tok := issueToken(t, api, "")
	body := &pausedBody{Reader: strings.NewReader("\x89PNG bytes"), pause: 150 * time.Millisecond}
	req := httptest.NewRequest(http.MethodPost, "/projects/p_slow_a/files/original?ext=png&w=1&h=1", body)
	req.Header.Set("Authorization", "Bearer "+tok)
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	if rec.Code != http.StatusCreated {
		t.Fatalf("code %d body %s, want 201", rec.Code, rec.Body.String())
	}
	if mem.SetFileCalls() != 1 {
		t.Fatalf("SetFile ran %d times, want 1", mem.SetFileCalls())
	}
}
