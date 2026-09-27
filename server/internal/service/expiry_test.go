package service

import (
	"context"
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

type liveIDs []string

func (l liveIDs) LiveProjectIDs() []string { return l }

// An expired project someone is editing live is deferred, not swept; with no hub, nothing is.
func TestExpiryDefersProjectsWithLiveEditors(t *testing.T) {
	st := testutil.NewMemStore()
	for _, id := range []string{"p_old_a", "p_old_b"} {
		st.Seed(protocol.ProjectRecord{ID: id, ExpiresAt: 10})
	}
	ids, err := Expiry{Store: st, Live: liveIDs{"p_old_b"}}.DeleteExpiredProjects(context.Background(), 100, 10)
	if err != nil || len(ids) != 1 || ids[0] != "p_old_a" {
		t.Fatalf("swept %v (%v), want only the idle project", ids, err)
	}
	if _, ok := st.Project("p_old_b"); !ok {
		t.Fatal("the project being edited was swept")
	}
	if ids, _ := (Expiry{Store: st}).DeleteExpiredProjects(context.Background(), 100, 10); len(ids) != 1 {
		t.Fatalf("with no live source the deferred project goes next pass: %v", ids)
	}
}
