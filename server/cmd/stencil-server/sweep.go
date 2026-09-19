package main

import (
	"context"
	"log"
	"sync"
	"time"
)

// sweepBatch bounds one DELETE ... RETURNING pass; sweepWorkers bounds the
// per-id filestore removal + publish, which is I/O the loop used to serialise.
const (
	sweepBatch   = 500
	sweepWorkers = 8
)

// dropEach drops each swept project over a bounded pool: thousands of ids should
// not run one-at-a-time, nor spawn one goroutine each.
func dropEach(ctx context.Context, drops projectDropper, ids []string) {
	work := make(chan string)
	var wg sync.WaitGroup
	for i := 0; i < sweepWorkers && i < len(ids); i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for id := range work {
				drops.Dropped(ctx, id)
			}
		}()
	}
	for _, id := range ids {
		work <- id
	}
	close(work)
	wg.Wait()
}

// startExpirySweep sweeps expired projects once, then every interval until ctx is cancelled, dropping
// filestore bytes and publishing a deleted event. A zero or negative interval disables it (no lazy expiry).
func startExpirySweep(ctx context.Context, wg *sync.WaitGroup, st expiredProjectDeleter, drops projectDropper, interval time.Duration) {
	if interval <= 0 {
		log.Printf("expiry sweep disabled (EXPIRY_SWEEP_MINUTES=0)")
		return
	}
	sweep := func() {
		total := 0
		// One batch per DB round trip; keep going while a pass comes back full, so
		// a large backlog still clears in one sweep without an unbounded delete.
		for {
			ids, err := st.DeleteExpiredProjects(ctx, time.Now().UnixMilli(), sweepBatch)
			if err != nil {
				log.Printf("expiry sweep: %v", err)
				break
			}
			dropEach(ctx, drops, ids)
			total += len(ids)
			if len(ids) < sweepBatch {
				break
			}
		}
		if total > 0 {
			log.Printf("expiry sweep: removed %d expired project(s)", total)
		}
	}
	wg.Add(1)
	go func() {
		defer wg.Done() // let run() join this goroutine before it closes st/b
		sweep()         // startup check
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				sweep()
			}
		}
	}()
}
