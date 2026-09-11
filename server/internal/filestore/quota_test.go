package filestore

// STORAGE_QUOTA_BYTES accounting: the cap, and what moves the meter.

import (
	"errors"
	"testing"

	"stencil/server/internal/protocol"
)

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
