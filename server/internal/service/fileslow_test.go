package service

// A slow upload spends its own time, not the store's: the copy runs under the caller's context, and every
// store call after it gets a fresh op timeout, so bytes that landed are still recorded.

import (
	"context"
	"io"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// expiringStore fails a SetFile whose context has already ended, as Postgres would.
type expiringStore struct{ *testutil.MemStore }

func (e expiringStore) SetFile(ctx context.Context, id string, f store.StoredFile) (protocol.ProjectRecord, error) {
	if err := ctx.Err(); err != nil {
		return protocol.ProjectRecord{}, err
	}
	return e.MemStore.SetFile(ctx, id, f)
}

// slowBody yields its bytes only after a pause longer than the op timeout.
type slowBody struct {
	io.Reader
	wait time.Duration
}

func (s *slowBody) Read(p []byte) (int, error) {
	if s.wait > 0 {
		time.Sleep(s.wait)
		s.wait = 0
	}
	return s.Reader.Read(p)
}

func TestASlowUploadStillRecordsItsFile(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	svc := NewFiles(expiringStore{st}, &fakeFiles{}, eventbus.NewInProc())
	svc.OpTimeout = 30 * time.Millisecond
	body := &slowBody{Reader: strings.NewReader("bytes"), wait: 120 * time.Millisecond}
	if _, err := svc.Store(context.Background(), put(protocol.KindOriginal), body); err != nil {
		t.Fatalf("an upload slower than one op timeout failed after its bytes landed: %v", err)
	}
	if st.SetFileCalls() != 1 {
		t.Fatalf("SetFile ran %d times, want 1", st.SetFileCalls())
	}
}
