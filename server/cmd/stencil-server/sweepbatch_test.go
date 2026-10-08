package main

import (
	"context"
	"strconv"
	"sync"
	"testing"
	"time"
)

func TestSweepBatchesBacklogAndBoundsWorkers(t *testing.T) {
	const backlog = testBatch*2 + 7
	expires := map[string]int64{}
	for i := 0; i < backlog; i++ {
		expires["p_old_"+strconv.Itoa(i)] = 1
	}
	st := &fakeSweepStore{expires: expires}
	fs := &fakeRemover{delay: 2 * time.Millisecond}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, sweepOf(st, sweepDrops(fs)), time.Hour) // startup pass only

	pollUntil(t, "the whole backlog to clear", func() bool { return len(fs.list()) == backlog })
	// 500 + 500 + 7: the short third pass ends the loop.
	if got := st.callCount(); got != 3 {
		t.Fatalf("%d delete calls for %d projects, want 3 batches", got, backlog)
	}
	if peak := fs.peak(); peak > testWorkers || peak < 2 {
		t.Fatalf("peak concurrent removals %d, want 2..%d", peak, testWorkers)
	}
}

// fakeSessionSweep holds a count of expired session rows and takes them a batch at a time.
type fakeSessionSweep struct {
	mu      sync.Mutex
	expired int
	calls   int
}

func (f *fakeSessionSweep) DeleteExpiredSessions(_ context.Context, _ int64, limit int) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	n := min(limit, f.expired)
	f.expired -= n
	return n, nil
}

func (f *fakeSessionSweep) state() (int, int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.expired, f.calls
}

// Expired session rows go in the same pass, batched like projects.
func TestSweepDeletesExpiredSessions(t *testing.T) {
	sessions := &fakeSessionSweep{expired: testBatch + 3}
	m := sweepOf(&fakeSweepStore{expires: map[string]int64{}}, sweepDrops(nil))
	m.sessions = sessions
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, m, time.Hour)
	pollUntil(t, "the expired sessions to clear", func() bool { left, _ := sessions.state(); return left == 0 })
	if _, calls := sessions.state(); calls != 2 {
		t.Fatalf("%d session batches, want 2", calls)
	}
}

// stuckSessionSweep blocks until its context ends, as a wedged database would.
type stuckSessionSweep struct{}

func (stuckSessionSweep) DeleteExpiredSessions(ctx context.Context, _ int64, _ int) (int, error) {
	<-ctx.Done()
	return 0, ctx.Err()
}

// A pass runs on the root context, so a wedged session DELETE must give up at the op timeout.
func TestSweepBoundsTheSessionDeleteByTheOpTimeout(t *testing.T) {
	m := sweepOf(&fakeSweepStore{expires: map[string]int64{}}, sweepDrops(nil))
	m.sessions, m.timeout = stuckSessionSweep{}, 50*time.Millisecond
	done := make(chan struct{})
	go func() { m.pass(context.Background()); close(done) }()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("the pass waited on a wedged store past its op timeout")
	}
}
