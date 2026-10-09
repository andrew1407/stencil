package filestore

// Staging: the bytes are metered but unseen until Commit; Discard gives back both, and a staged file a
// crash left is the reconcile pass's.

import (
	"bytes"
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"stencil/server/internal/protocol"
)

func stage(t *testing.T, s *Store, ext string, data []byte) Staged {
	t.Helper()
	st, err := s.StageStreamAs(validID, protocol.KindOriginal, ext, bytes.NewReader(data), Charge{})
	if err != nil {
		t.Fatalf("stage: %v", err)
	}
	return st
}

func TestAStagedUploadIsUnseenUntilCommit(t *testing.T) {
	s := newTestStore(t)
	old, err := s.Put(validID, protocol.KindOriginal, "png", []byte("old"))
	if err != nil {
		t.Fatal(err)
	}
	st := stage(t, s, "jpg", []byte("new"))
	if rel, err := s.FindByKind(validID, protocol.KindOriginal); err != nil || rel != old {
		t.Fatalf("while staged FindByKind = %q, %v; want the old %q", rel, err, old)
	}
	if err := st.Commit(); err != nil {
		t.Fatal(err)
	}
	if got, _ := s.GetByRelPath(st.Rel()); string(got) != "new" {
		t.Fatalf("committed bytes %q", got)
	}
	if list, _ := s.List(validID); len(list) != 1 || list[0] != st.Rel() {
		t.Fatalf("after commit the project holds %v, want only %s", list, st.Rel())
	}
}

func TestADiscardedStageLeavesTheOldBytesAndFreesItsQuota(t *testing.T) {
	for _, ext := range []string{"png", "jpg"} {
		s := newQuotaStore(t, 100)
		old, err := s.Put(validID, protocol.KindOriginal, "png", make([]byte, 40))
		if err != nil {
			t.Fatal(err)
		}
		if err := stage(t, s, ext, make([]byte, 50)).Discard(); err != nil {
			t.Fatal(err)
		}
		if list, _ := s.List(validID); len(list) != 1 || list[0] != old {
			t.Fatalf("%s: after discard the project holds %v, want only %s", ext, list, old)
		}
		if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 60)); err != nil {
			t.Fatalf("%s: the discarded stage still holds quota: %v", ext, err)
		}
	}
}

func TestReconcileDropsAStaleStagedFile(t *testing.T) {
	s := newTestStore(t)
	st := stage(t, s, "png", []byte("x")).(*staged)
	past := time.Now().Add(-2 * time.Hour)
	if err := os.Chtimes(st.path, past, past); err != nil {
		t.Fatal(err)
	}
	got, err := s.Reconcile(context.Background(), owners(validID), time.Hour)
	if err != nil || got.Temps != 1 {
		t.Fatalf("reconcile: %+v %v", got, err)
	}
	if entries, _ := os.ReadDir(filepath.Dir(st.path)); len(entries) != 0 {
		t.Fatalf("left %v", entries)
	}
}
