package filestore

// Charge.Admit: the writer's ledger check wraps the commit, sees the committed size, and a refusal
// leaves neither the file nor its temp; the owner lookup is done before it starts, never inside it.

import (
	"bytes"
	"errors"
	"os"
	"testing"

	"stencil/server/internal/protocol"
)

func TestAdmitWrapsTheCommit(t *testing.T) {
	s := newOwnerStore(t, Quotas{PerOwner: 1 << 20})
	var steps []string
	peers := func() ([]string, error) { steps = append(steps, "peers"); return []string{ownedA}, nil }
	admit := func(n int64, commit func() error) error {
		steps = append(steps, "admit")
		if n != 7 {
			t.Fatalf("admit saw %d bytes, want the 7 committed", n)
		}
		if _, err := s.FindByKind(ownedA, protocol.KindOriginal); !errors.Is(err, ErrNotFound) {
			t.Fatal("the file landed before admit ran its commit")
		}
		return commit()
	}
	if _, err := s.PutStreamAs(ownedA, protocol.KindOriginal, "png", bytes.NewReader(make([]byte, 7)), Charge{Peers: peers, Admit: admit}); err != nil {
		t.Fatal(err)
	}
	if len(steps) != 2 || steps[0] != "peers" || steps[1] != "admit" {
		t.Fatalf("steps = %v, want the owner lookup before the ledger check", steps)
	}
	if _, err := s.FindByKind(ownedA, protocol.KindOriginal); err != nil {
		t.Fatalf("an admitted upload is stored: %v", err)
	}
}

func TestAdmitRefusalStoresNothing(t *testing.T) {
	s := newOwnerStore(t, Quotas{})
	refuse := func(int64, func() error) error { return ErrQuotaExceeded }
	_, err := s.PutStreamAs(ownedB, protocol.KindResult, "png", bytes.NewReader([]byte("abc")), Charge{Admit: refuse})
	if !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("err = %v, want the refusal passed through", err)
	}
	dir, _ := s.projectDir(ownedB)
	if entries, _ := os.ReadDir(dir); len(entries) != 0 {
		t.Fatalf("a refused upload left %d entries behind", len(entries))
	}
}
