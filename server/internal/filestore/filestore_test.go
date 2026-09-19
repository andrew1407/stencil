package filestore

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"stencil/server/internal/protocol"
)

const validID = "p_abc123_def456"

func newTestStore(t *testing.T) *Store {
	t.Helper()
	s, err := New(t.TempDir())
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	return s
}

func TestPutGetRoundTrip(t *testing.T) {
	s := newTestStore(t)
	want := []byte("\x89PNG stub bytes")
	rel, err := s.Put(validID, protocol.KindOriginal, "png", want)
	if err != nil {
		t.Fatalf("Put: %v", err)
	}
	if rel != "projects/"+validID+"/original.png" {
		t.Fatalf("unexpected rel path %q", rel)
	}
	got, err := s.Get(validID, protocol.KindOriginal, "png")
	if err != nil {
		t.Fatalf("Get: %v", err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("round trip mismatch")
	}
	// GetByRelPath should resolve the recorded path too.
	got2, err := s.GetByRelPath(rel)
	if err != nil || !bytes.Equal(got2, want) {
		t.Fatalf("GetByRelPath: %v", err)
	}
}

func TestPutIsAtomicOverwrite(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Put(validID, protocol.KindResult, "jpg", []byte("v1")); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Put(validID, protocol.KindResult, "jpg", []byte("v2longer")); err != nil {
		t.Fatal(err)
	}
	got, err := s.Get(validID, protocol.KindResult, "jpg")
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != "v2longer" {
		t.Fatalf("overwrite failed, got %q", got)
	}
	// No stray temp files left behind.
	entries, _ := os.ReadDir(filepath.Join(s.Root(), "projects", validID))
	for _, e := range entries {
		if filepath.Ext(e.Name()) == "" && len(e.Name()) > 4 && e.Name()[:4] == ".tmp" {
			t.Fatalf("leftover temp file %s", e.Name())
		}
	}
}

func TestGetMissingIsNotFound(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Get(validID, protocol.KindOriginal, "png"); err != ErrNotFound {
		t.Fatalf("want ErrNotFound, got %v", err)
	}
}

func TestRemoveAndList(t *testing.T) {
	s := newTestStore(t)
	s.Put(validID, protocol.KindOriginal, "png", []byte("a"))
	s.Put(validID, protocol.KindResult, "png", []byte("b"))
	files, err := s.List(validID)
	if err != nil {
		t.Fatal(err)
	}
	if len(files) != 2 {
		t.Fatalf("want 2 files, got %v", files)
	}
	if err := s.Remove(validID); err != nil {
		t.Fatal(err)
	}
	files, _ = s.List(validID)
	if len(files) != 0 {
		t.Fatalf("expected empty after remove, got %v", files)
	}
	// Removing again is a no-op.
	if err := s.Remove(validID); err != nil {
		t.Fatalf("double remove: %v", err)
	}
}

// TestRemoveKind pins the per-kind delete used by the files DELETE route: exactly the named kind's file
// goes away (any extension), other kinds' bytes survive, and deleting an absent kind is a no-op.
func TestRemoveKind(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Put(validID, protocol.KindChat, "json", []byte(`{"version":1}`)); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Put(validID, protocol.KindOriginal, "png", []byte("img")); err != nil {
		t.Fatal(err)
	}
	if err := s.RemoveKind(validID, protocol.KindChat); err != nil {
		t.Fatalf("remove chat: %v", err)
	}
	if _, err := s.FindByKind(validID, protocol.KindChat); !errors.Is(err, ErrNotFound) {
		t.Fatalf("chat should be gone, got %v", err)
	}
	if _, err := s.FindByKind(validID, protocol.KindOriginal); err != nil {
		t.Fatalf("original must survive a chat delete: %v", err)
	}
	// Idempotent: deleting again (or a never-written kind) is not an error.
	if err := s.RemoveKind(validID, protocol.KindChat); err != nil {
		t.Fatalf("second remove should be a no-op: %v", err)
	}
	if err := s.RemoveKind(validID, protocol.KindVideo); err != nil {
		t.Fatalf("absent kind remove should be a no-op: %v", err)
	}
}

// Re-uploading a kind with a new extension drops the old-extension file, so FindByKind can never
// resolve to stale bytes. Other kinds' files are untouched.
func TestPutReplacesStaleSiblingExtension(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Put(validID, protocol.KindVideo, "mp4", []byte("old")); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Put(validID, protocol.KindOriginal, "png", []byte("keep")); err != nil {
		t.Fatal(err)
	}
	rel, err := s.Put(validID, protocol.KindVideo, "webm", []byte("new"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := s.Get(validID, protocol.KindVideo, "mp4"); err != ErrNotFound {
		t.Fatalf("stale video.mp4 should be gone, got err %v", err)
	}
	if got, err := s.FindByKind(validID, protocol.KindVideo); err != nil || got != rel {
		t.Fatalf("FindByKind = %q, %v; want %q", got, err, rel)
	}
	if _, err := s.FindByKind(validID, protocol.KindResult); err != ErrNotFound {
		t.Fatalf("FindByKind for absent kind should be ErrNotFound, got %v", err)
	}
	files, err := s.List(validID)
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]bool{rel: true, "projects/" + validID + "/original.png": true}
	if len(files) != len(want) {
		t.Fatalf("unexpected files after re-upload: %v", files)
	}
	for _, f := range files {
		if !want[f] {
			t.Fatalf("unexpected file %q after re-upload", f)
		}
	}
}

// ----- aggregate storage quota (NewWithQuota / ErrQuotaExceeded) -----
