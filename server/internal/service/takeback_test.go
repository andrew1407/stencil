package service

// The take-backs: an upload whose record write failed leaves no bytes behind, and a create whose inline
// original was refused takes its row back even when the request's context is what ended.

import (
	"context"
	"errors"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// brokenRecord fails every SetFile as an unreachable database would.
type brokenRecord struct{ *testutil.MemStore }

func (brokenRecord) SetFile(context.Context, string, store.StoredFile) (protocol.ProjectRecord, error) {
	return protocol.ProjectRecord{}, errors.New("db down")
}

func TestARecordFailureTakesTheBytesBack(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	files := &fakeFiles{}
	svc := NewFiles(brokenRecord{st}, files, eventbus.NewInProc())
	ctx, cancel := context.WithCancel(context.Background())
	_, _, err := svc.put(ctx, put(protocol.KindOriginal), strings.NewReader("bytes"))
	cancel()
	if !errors.Is(err, ErrRecordFile) {
		t.Fatalf("put = %v, want ErrRecordFile", err)
	}
	if len(files.removedKind) != 1 || files.removedKind[0] != "p_1/original" || len(files.kinds()) != 0 {
		t.Fatalf("removed %v, left %v; want the unrecorded original gone", files.removedKind, files.kinds())
	}
}

// boundedDelete records the context the take-back's DeleteProject ran under.
type boundedDelete struct {
	*testutil.MemStore
	live, bounded bool
}

func (b *boundedDelete) DeleteProject(ctx context.Context, id string) error {
	_, b.bounded = ctx.Deadline()
	b.live = ctx.Err() == nil
	return b.MemStore.DeleteProject(ctx, id)
}

func TestARefusedOriginalTakesTheRowBackPastACancelledRequest(t *testing.T) {
	st := &boundedDelete{MemStore: testutil.NewMemStore()}
	files := &fakeFiles{putErr: errors.New("disk full")}
	svc := NewProjects(st, files, eventbus.NewInProc(), 0)
	svc.Originals = NewFiles(st, files, svc.Bus)
	svc.Originals.OpTimeout = time.Second
	ctx, cancel := context.WithCancel(context.Background())
	cancel() // the request is already gone when the original is refused
	req := protocol.CreateProjectRequest{Name: "n", HasImage: true, OriginalContent: "data:image/png;base64,Zmlyc3Q="}
	if _, err := svc.Create(ctx, "s_1", req); !errors.Is(err, ErrStoreOriginal) {
		t.Fatalf("create = %v, want ErrStoreOriginal", err)
	}
	if !st.live || !st.bounded {
		t.Fatalf("the take-back ran on a live context %v with a deadline %v; want both", st.live, st.bounded)
	}
	if list, _ := st.ListProjects(context.Background(), listAll); len(list) != 0 {
		t.Fatalf("a refused create left %d rows", len(list))
	}
}
