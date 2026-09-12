package main

import (
	"context"
	"strconv"
	"sync"
	"testing"
	"time"
)

// A backlog larger than one batch is deleted a batch at a time until a pass comes
// back short, and the per-id filestore work runs on a bounded pool rather than
// one id after another.
func TestSweepBatchesBacklogAndBoundsWorkers(t *testing.T) {
	const backlog = sweepBatch*2 + 7
	expires := map[string]int64{}
	for i := 0; i < backlog; i++ {
		expires["p_old_"+strconv.Itoa(i)] = 1
	}
	st := &fakeSweepStore{expires: expires}
	fs := &fakeRemover{delay: 2 * time.Millisecond}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, st, sweepDrops(fs), time.Hour) // startup pass only

	pollUntil(t, "the whole backlog to clear", func() bool { return len(fs.list()) == backlog })
	// 500 + 500 + 7: the short third pass ends the loop.
	if got := st.callCount(); got != 3 {
		t.Fatalf("%d delete calls for %d projects, want 3 batches", got, backlog)
	}
	if peak := fs.peak(); peak > sweepWorkers || peak < 2 {
		t.Fatalf("peak concurrent removals %d, want 2..%d", peak, sweepWorkers)
	}
}
