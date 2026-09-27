package filestore

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"stencil/server/internal/protocol"
)

// owners answers the reconcile pass's row check from a fixed set; "p_broken_x" fails the lookup.
func owners(ids ...string) func(context.Context, string) (bool, error) {
	set := map[string]bool{}
	for _, id := range ids {
		set[id] = true
	}
	return func(_ context.Context, id string) (bool, error) {
		if id == "p_broken_x" {
			return false, errors.New("db down")
		}
		return set[id], nil
	}
}

// A directory no row owns goes; an owned one and one whose check failed stay, and so does anything
// that is not an id-shaped directory.
func TestReconcileDropsOrphanDirectories(t *testing.T) {
	s := newQuotaStore(t, 100)
	for _, id := range []string{"p_kept_a", "p_orphan_b", "p_broken_x"} {
		if _, err := s.Put(id, protocol.KindOriginal, "png", make([]byte, 30)); err != nil {
			t.Fatal(err)
		}
	}
	stray := filepath.Join(s.Root(), "projects", "not-an-id")
	if err := os.MkdirAll(stray, 0o755); err != nil {
		t.Fatal(err)
	}
	got, err := s.Reconcile(context.Background(), owners("p_kept_a"), time.Hour)
	if err != nil || got.Orphans != 1 {
		t.Fatalf("reconcile: %+v %v, want one orphan", got, err)
	}
	for id, want := range map[string]bool{"p_kept_a": true, "p_orphan_b": false, "p_broken_x": true} {
		if _, err := s.FindByKind(id, protocol.KindOriginal); (err == nil) != want {
			t.Errorf("%s present = %v, want %v", id, err == nil, want)
		}
	}
	if _, err := os.Stat(stray); err != nil {
		t.Fatal("a directory the store never writes was touched")
	}
	// The orphan's 30 bytes went back to the quota: 60 held, so 40 more fit.
	if _, err := s.Put("p_kept_a", protocol.KindResult, "png", make([]byte, 40)); err != nil {
		t.Fatalf("the reclaimed bytes were not credited: %v", err)
	}
}

// Only a temp file older than the cutoff is dead; a newer one may be a live upload.
func TestReconcileDropsOnlyStaleTemps(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Put(validID, protocol.KindOriginal, "png", []byte("x")); err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(s.Root(), "projects", validID)
	old, fresh := filepath.Join(dir, ".tmp-old"), filepath.Join(dir, ".tmp-fresh")
	for _, p := range []string{old, fresh} {
		if err := os.WriteFile(p, []byte("partial"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	past := time.Now().Add(-2 * time.Hour)
	if err := os.Chtimes(old, past, past); err != nil {
		t.Fatal(err)
	}
	got, err := s.Reconcile(context.Background(), owners(validID), time.Hour)
	if err != nil || got.Temps != 1 || got.Orphans != 0 {
		t.Fatalf("reconcile: %+v %v", got, err)
	}
	if _, err := os.Stat(old); !os.IsNotExist(err) {
		t.Fatal("the stale temp file survived")
	}
	if _, err := os.Stat(fresh); err != nil {
		t.Fatal("a fresh temp file (a live upload) was removed")
	}
}
