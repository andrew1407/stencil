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

// TestRemoveKind pins the per-kind delete used by the files DELETE route:
// exactly the named kind's file goes away (any extension), other kinds'
// bytes survive, and deleting an absent kind is a no-op.
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

// TestPutReplacesStaleSiblingExtension pins that re-uploading a kind with a new
// extension drops the old-extension file, so kind-based lookups (FindByKind)
// can never resolve to stale bytes. Other kinds' files are untouched.
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

// ----- aggregate storage quota (NewWithQuota / ErrQuotaExceeded) -----

func newQuotaStore(t *testing.T, quota int64) *Store {
	t.Helper()
	s, err := NewWithQuota(t.TempDir(), quota)
	if err != nil {
		t.Fatalf("NewWithQuota: %v", err)
	}
	return s
}

func TestQuotaRejectsWritePastTheCap(t *testing.T) {
	s := newQuotaStore(t, 100)
	if _, err := s.Put(validID, protocol.KindOriginal, "png", make([]byte, 60)); err != nil {
		t.Fatalf("under-quota Put: %v", err)
	}
	// 60 + 60 > 100: the aggregate cap, not the per-file size, is what trips.
	if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 60)); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("over-quota Put: got %v, want ErrQuotaExceeded", err)
	}
	// The rejected write must not have consumed budget.
	if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 40)); err != nil {
		t.Fatalf("fitting Put after a rejection: %v", err)
	}
}

func TestQuotaAccountsReplacementsAndRemovals(t *testing.T) {
	s := newQuotaStore(t, 100)
	if _, err := s.Put(validID, protocol.KindOriginal, "png", make([]byte, 90)); err != nil {
		t.Fatalf("Put: %v", err)
	}
	// Replacing a kind re-uses its budget (delta, not sum)...
	if _, err := s.Put(validID, protocol.KindOriginal, "png", make([]byte, 95)); err != nil {
		t.Fatalf("replacement within quota: %v", err)
	}
	// ...including a replacement that switches extension (stale-sibling cleanup).
	if _, err := s.Put(validID, protocol.KindOriginal, "jpg", make([]byte, 95)); err != nil {
		t.Fatalf("cross-extension replacement: %v", err)
	}
	// RemoveKind frees its bytes for new writes.
	if err := s.RemoveKind(validID, protocol.KindOriginal); err != nil {
		t.Fatalf("RemoveKind: %v", err)
	}
	if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 90)); err != nil {
		t.Fatalf("Put after RemoveKind: %v", err)
	}
	// Remove (whole project) frees everything.
	if err := s.Remove(validID); err != nil {
		t.Fatalf("Remove: %v", err)
	}
	if _, err := s.Put("p_other1_id2", protocol.KindOriginal, "png", make([]byte, 100)); err != nil {
		t.Fatalf("Put after Remove: %v", err)
	}
}

// Pre-existing bytes count: the starting usage is computed from disk, so a
// restart cannot forget what is already stored.
func TestQuotaCountsPreexistingBytes(t *testing.T) {
	dir := t.TempDir()
	s0, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := s0.Put(validID, protocol.KindOriginal, "png", make([]byte, 80)); err != nil {
		t.Fatal(err)
	}
	s, err := NewWithQuota(dir, 100)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 30)); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("preexisting bytes not counted: got %v", err)
	}
	if _, err := s.Put(validID, protocol.KindResult, "png", make([]byte, 20)); err != nil {
		t.Fatalf("fitting Put: %v", err)
	}
}

// Quota 0 keeps the unlimited behavior (and skips accounting entirely).
func TestQuotaZeroIsUnlimited(t *testing.T) {
	s := newQuotaStore(t, 0)
	if _, err := s.Put(validID, protocol.KindOriginal, "png", make([]byte, 1<<16)); err != nil {
		t.Fatalf("Put with quota 0: %v", err)
	}
}
