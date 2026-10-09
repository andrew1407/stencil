package service

import (
	"context"
	"testing"
	"time"
)

// deadlineStore records whether each store call it saw carried a deadline.
type deadlineStore struct{ saw []bool }

func (d *deadlineStore) DeleteExpiredProjects(ctx context.Context, _ int64, _ int) ([]string, error) {
	_, ok := ctx.Deadline()
	d.saw = append(d.saw, ok)
	return nil, nil
}

// The sweep runs on the root context, so its store call carries an op timeout of its own.
func TestExpiryBoundsEachStoreCall(t *testing.T) {
	d := &deadlineStore{}
	if _, err := (Expiry{Store: d, OpTimeout: time.Second}).DeleteExpiredProjects(context.Background(), 1, 1); err != nil {
		t.Fatal(err)
	}
	if len(d.saw) != 1 || !d.saw[0] {
		t.Fatalf("deadlines seen %v, want one on the call", d.saw)
	}
}

// Without an op timeout the caller's context is used as it is.
func TestExpiryWithoutATimeoutKeepsTheCallersContext(t *testing.T) {
	d := &deadlineStore{}
	if _, err := (Expiry{Store: d}).DeleteExpiredProjects(context.Background(), 1, 1); err != nil {
		t.Fatal(err)
	}
	if len(d.saw) != 1 || d.saw[0] {
		t.Fatalf("deadlines seen %v, want none", d.saw)
	}
}
