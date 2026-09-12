package service

// The upload saga's failure half: a project row that disappears mid-upload must
// leave no bytes behind, and must take only its own kind with it.

import (
	"context"
	"errors"
	"strings"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

func fileSvc(t *testing.T) (*FileService, *testutil.MemStore, *fakeFiles) {
	t.Helper()
	st, files := testutil.NewMemStore(), &fakeFiles{}
	return NewFiles(st, files, bus.NewInProc()), st, files
}

func put(kind string) FilePut { return FilePut{ID: "p_1", Kind: kind, Ext: "png", W: 4, H: 2} }

// The happy path records the file and reports where it landed.
func TestStoreRecordsAnOriginal(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	resp, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("bytes"))
	if err != nil {
		t.Fatal(err)
	}
	if resp.Path == "" || resp.W != 4 || resp.H != 2 {
		t.Fatalf("response = %+v", resp)
	}
	if st.SetFileCalls() != 1 {
		t.Fatalf("SetFile ran %d times, want 1", st.SetFileCalls())
	}
	if len(files.kinds()) != 1 {
		t.Fatalf("stored = %v, want one file", files.kinds())
	}
}

// A missing project is refused before any bytes are written.
func TestStoreRefusesAnUnknownProject(t *testing.T) {
	svc, _, files := fileSvc(t)
	_, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("bytes"))
	if !errors.Is(err, store.ErrNotFound) {
		t.Fatalf("err = %v, want ErrNotFound", err)
	}
	if len(files.kinds()) != 0 {
		t.Fatalf("bytes written for a missing project: %v", files.kinds())
	}
}

// The sweep deleting the row between the check and SetFile must not orphan the
// bytes: the compensating delete takes them back.
func TestStoreCompensatesWhenTheRowGoesDuringSetFile(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	st.SweepOnWrite("p_1")
	_, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("bytes"))
	if !errors.Is(err, store.ErrNotFound) {
		t.Fatalf("err = %v, want ErrNotFound", err)
	}
	if len(files.kinds()) != 0 {
		t.Fatalf("bytes survived a vanished project: %v", files.kinds())
	}
	if len(files.removedKind) != 1 || files.removedKind[0] != "p_1/original" {
		t.Fatalf("compensation = %v, want [p_1/original]", files.removedKind)
	}
}

// A filestore-only kind has no row to write, so its guard is the post-write
// re-check — and it too compensates rather than orphaning bytes.
func TestStoreCompensatesForAFilestoreOnlyKind(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	st.GoneAfterGets("p_1", 1) // the pre-check sees it, the re-check does not
	_, err := svc.Store(context.Background(), put(protocol.KindVideo), strings.NewReader("bytes"))
	if !errors.Is(err, store.ErrNotFound) {
		t.Fatalf("err = %v, want ErrNotFound", err)
	}
	if st.SetFileCalls() != 0 {
		t.Fatal("a filestore-only kind must never touch the project record")
	}
	if len(files.removedKind) != 1 || files.removedKind[0] != "p_1/video" {
		t.Fatalf("compensation = %v, want [p_1/video]", files.removedKind)
	}
}

// A write failure is passed through untouched so the caller can still tell a
// quota refusal from a broken disk.
func TestStorePassesAWriteFailureThrough(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	boom := errors.New("disk on fire")
	files.putErr = boom
	if _, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("b")); !errors.Is(err, boom) {
		t.Fatalf("err = %v, want the store's own error", err)
	}
}
