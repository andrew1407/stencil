package filestore

import (
	"bytes"
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
	want := []byte("\x89PNG fake bytes")
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

func TestPathTraversalRejected(t *testing.T) {
	s := newTestStore(t)
	bad := []struct {
		id, kind, ext string
	}{
		{"../etc", protocol.KindOriginal, "png"}, // bad id
		{"p_x_y/../../../etc", protocol.KindOriginal, "png"},
		{validID, "passwd", "png"},                      // bad kind
		{validID, "../original", "png"},                 // kind traversal
		{validID, protocol.KindOriginal, "../../etc/x"}, // ext traversal
		{validID, protocol.KindOriginal, "p/n"},         // ext separator
		{"p_abc_../def", protocol.KindOriginal, "png"},  // id with traversal
		{"/abs/p_a_b", protocol.KindOriginal, "png"},    // absolute-ish id
	}
	for _, c := range bad {
		if _, err := s.Put(c.id, c.kind, c.ext, []byte("x")); err == nil {
			t.Fatalf("expected rejection for id=%q kind=%q ext=%q", c.id, c.kind, c.ext)
		}
		if _, err := s.Get(c.id, c.kind, c.ext); err == nil {
			t.Fatalf("expected Get rejection for id=%q kind=%q ext=%q", c.id, c.kind, c.ext)
		}
	}
	// Nothing should have been written outside the project tree.
	if _, err := os.Stat(filepath.Join(filepath.Dir(s.Root()), "etc")); !os.IsNotExist(err) {
		t.Fatalf("traversal wrote outside root")
	}
}

func TestGetByRelPathConfined(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.GetByRelPath("../../../etc/passwd"); err == nil {
		t.Fatalf("expected confinement error")
	}
}

func TestSymlinkEscapeBlocked(t *testing.T) {
	s := newTestStore(t)
	// Create a sibling secret outside the root.
	outside := filepath.Join(filepath.Dir(s.Root()), "outside")
	if err := os.MkdirAll(outside, 0o755); err != nil {
		t.Fatal(err)
	}
	// projects/<id> -> outside (symlink escaping the root).
	projects := filepath.Join(s.Root(), "projects")
	os.MkdirAll(projects, 0o755)
	link := filepath.Join(projects, validID)
	if err := os.Symlink(outside, link); err != nil {
		t.Skipf("symlink unsupported: %v", err)
	}
	if _, err := s.Put(validID, protocol.KindOriginal, "png", []byte("x")); err == nil {
		t.Fatalf("expected symlink escape to be blocked")
	}
}

func TestExtNormalization(t *testing.T) {
	s := newTestStore(t)
	rel, err := s.Put(validID, protocol.KindOriginal, ".PNG", []byte("x"))
	if err != nil {
		t.Fatal(err)
	}
	if filepath.Ext(rel) != ".png" {
		t.Fatalf("ext not normalized: %q", rel)
	}
	// Empty ext defaults to bin.
	rel2, err := s.Put("p_a_b", protocol.KindResult, "", []byte("y"))
	if err != nil || filepath.Ext(rel2) != ".bin" {
		t.Fatalf("empty ext default failed: %q %v", rel2, err)
	}
}
