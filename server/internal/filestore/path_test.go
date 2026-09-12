package filestore

// Path confinement: traversal, the kind allowlist, symlink escape, extensions.

import (
	"os"
	"path/filepath"
	"testing"

	"stencil/server/internal/protocol"
)

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

// TestFileKindAllowlist pins the kind table: original/result plus the LLM-era
// video and variant1..variant8 kinds are accepted, everything else rejected.
func TestFileKindAllowlist(t *testing.T) {
	s := newTestStore(t)
	good := []string{
		protocol.KindOriginal, protocol.KindResult, protocol.KindVideo,
		protocol.KindChat, "variant1", "variant3", "variant8",
	}
	for _, k := range good {
		if _, err := s.Put(validID, k, "png", []byte("x")); err != nil {
			t.Fatalf("kind %q should be accepted: %v", k, err)
		}
		if _, err := s.Get(validID, k, "png"); err != nil {
			t.Fatalf("kind %q round trip failed: %v", k, err)
		}
	}
	bad := []string{
		"variant0", "variant9", "variantx", "variant", "variant10",
		"videos", "Variant1", "variant1/../original", "chats", "Chat",
	}
	for _, k := range bad {
		if _, err := s.Put(validID, k, "png", []byte("x")); err == nil {
			t.Fatalf("kind %q should be rejected", k)
		}
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
