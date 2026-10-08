package service

import (
	"context"
	"testing"
	"time"

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

// deadlineStore records whether each store call it saw carried a deadline.
type deadlineStore struct{ saw []bool }

func (d *deadlineStore) DeleteExpiredProjects(ctx context.Context, _ int64, _ int, _ []string) ([]string, error) {
	_, ok := ctx.Deadline()
	d.saw = append(d.saw, ok)
	return nil, nil
}

func (d *deadlineStore) LiveElsewhere(ctx context.Context) ([]string, error) {
	_, ok := ctx.Deadline()
	d.saw = append(d.saw, ok)
	return nil, nil
}

// The sweep runs on the root context, so each of its store calls carries the op timeout of its own.
func TestExpiryBoundsEachStoreCall(t *testing.T) {
	d := &deadlineStore{}
	if _, err := (Expiry{Store: d, Remote: d, OpTimeout: time.Second}).DeleteExpiredProjects(context.Background(), 1, 1); err != nil {
		t.Fatal(err)
	}
	if len(d.saw) != 2 || !d.saw[0] || !d.saw[1] {
		t.Fatalf("deadlines seen %v, want one on each of the two calls", d.saw)
	}
}
