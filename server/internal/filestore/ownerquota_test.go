package filestore

// STORAGE_QUOTA_PER_OWNER_BYTES: one owner's projects share a cap, other owners and ownerless
// projects do not, and the check holds under the same lock as the aggregate one.

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"sync"
	"testing"

	"stencil/server/internal/protocol"
)

const (
	ownedA = "p_owna_1"
	ownedB = "p_owna_2"
	other  = "p_ownb_1"
)

func peersOf(ids ...string) func() ([]string, error) {
	return func() ([]string, error) { return ids, nil }
}

func putAs(s *Store, id, kind string, n int, peers func() ([]string, error)) error {
	_, err := s.PutStreamAs(id, kind, "png", bytes.NewReader(make([]byte, n)), Charge{Peers: peers})
	return err
}

func newOwnerStore(t *testing.T, q Quotas) *Store {
	t.Helper()
	s, err := NewWithQuotas(t.TempDir(), q)
	if err != nil {
		t.Fatalf("NewWithQuotas: %v", err)
	}
	return s
}

func TestOwnerQuotaCapsOneOwnersProjectsTogether(t *testing.T) {
	s := newOwnerStore(t, Quotas{PerOwner: 100})
	mine := peersOf(ownedA, ownedB)
	if err := putAs(s, ownedA, protocol.KindOriginal, 60, mine); err != nil {
		t.Fatalf("first upload: %v", err)
	}
	// 60 + 60 > 100 across two projects of one owner: the owner, not the project, is capped.
	if err := putAs(s, ownedB, protocol.KindOriginal, 60, mine); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("second project of the owner: got %v, want ErrQuotaExceeded", err)
	}
	if _, err := s.FindByKind(ownedB, protocol.KindOriginal); !errors.Is(err, ErrNotFound) {
		t.Fatalf("a refused upload left bytes behind: %v", err)
	}
	// Another owner, and a project with no owner, answer only to the (unset) aggregate cap.
	if err := putAs(s, other, protocol.KindOriginal, 90, peersOf(other)); err != nil {
		t.Fatalf("another owner's upload: %v", err)
	}
	if err := putAs(s, "p_noowner_1", protocol.KindOriginal, 500, peersOf()); err != nil {
		t.Fatalf("ownerless upload: %v", err)
	}
	// Replacing a kind re-uses its own bytes; freeing them makes room for the owner's other project.
	if err := putAs(s, ownedA, protocol.KindOriginal, 95, mine); err != nil {
		t.Fatalf("replacement within the owner's quota: %v", err)
	}
	if err := s.RemoveKind(ownedA, protocol.KindOriginal); err != nil {
		t.Fatal(err)
	}
	if err := putAs(s, ownedB, protocol.KindOriginal, 100, mine); err != nil {
		t.Fatalf("upload after the owner freed bytes: %v", err)
	}
}

// With no per-owner quota the owner is never looked up: the default costs no database read.
func TestOwnerQuotaOffNeverAsksForPeers(t *testing.T) {
	asked := func() ([]string, error) { t.Fatal("peers asked with the per-owner quota off"); return nil, nil }
	for name, q := range map[string]Quotas{"unmetered": {}, "aggregate only": {Total: 1 << 20}} {
		s := newOwnerStore(t, q)
		if err := putAs(s, ownedA, protocol.KindOriginal, 60, asked); err != nil {
			t.Fatalf("%s: %v", name, err)
		}
	}
}

// Both caps apply at once, and a failed owner lookup refuses the upload rather than skipping the cap.
func TestOwnerQuotaWithAggregateAndLookupFailure(t *testing.T) {
	s := newOwnerStore(t, Quotas{Total: 100, PerOwner: 80})
	if err := putAs(s, ownedA, protocol.KindOriginal, 70, peersOf(ownedA)); err != nil {
		t.Fatal(err)
	}
	if err := putAs(s, other, protocol.KindOriginal, 40, peersOf(other)); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("aggregate cap: got %v", err)
	}
	if err := putAs(s, ownedA, protocol.KindResult, 20, peersOf(ownedA)); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("owner cap: got %v", err)
	}
	boom := errors.New("lookup failed")
	if err := putAs(s, other, protocol.KindOriginal, 10, func() ([]string, error) { return nil, boom }); !errors.Is(err, boom) {
		t.Fatalf("lookup failure: got %v, want it passed through", err)
	}
}

// An upload's temp file is not yet the owner's: only committed bytes count.
func TestOwnerQuotaIgnoresUploadTemps(t *testing.T) {
	s := newOwnerStore(t, Quotas{PerOwner: 100})
	dir, err := s.projectDir(ownedA)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, tmpPrefix+"stale"), make([]byte, 90), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := putAs(s, ownedA, protocol.KindOriginal, 100, peersOf(ownedA)); err != nil {
		t.Fatalf("a temp file counted against the owner: %v", err)
	}
}

// Concurrent uploads into one owner's projects: the owner check runs under the meter's lock with the
// rename, so no two uploads both see room for themselves.
func TestOwnerQuotaHoldsUnderConcurrentUploads(t *testing.T) {
	s := newOwnerStore(t, Quotas{PerOwner: 100})
	ids := []string{"p_c_1", "p_c_2", "p_c_3", "p_c_4", "p_c_5", "p_c_6", "p_c_7", "p_c_8"}
	var wg sync.WaitGroup
	var mu sync.Mutex
	stored := 0
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			err := putAs(s, id, protocol.KindOriginal, 30, peersOf(ids...))
			if err != nil && !errors.Is(err, ErrQuotaExceeded) {
				t.Error(err)
			}
			if err == nil {
				mu.Lock()
				stored++
				mu.Unlock()
			}
		}(id)
	}
	wg.Wait()
	if stored != 3 {
		t.Fatalf("%d uploads of 30 B fit a 100 B owner quota, want exactly 3", stored)
	}
	onDisk, err := dirSize(s.Root())
	if err != nil {
		t.Fatal(err)
	}
	if onDisk > 100 {
		t.Fatalf("the owner holds %d B past its 100 B quota", onDisk)
	}
}
