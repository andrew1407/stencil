package main

import (
	"context"
	"log"
	"sync"
	"time"
)

// Interface seams over *store.Store / *filestore.Store so the sweep is testable
// without Postgres or a real filestore. Behavior is unchanged.
type expiredProjectDeleter interface {
	DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error)
}

// projectDropper is the tail of service.ProjectService.Delete: drop a removed
// project's bytes and announce it. Shared rather than re-implemented, so the
// sweep and DELETE /projects/{id} cannot drift apart.
type projectDropper interface {
	Dropped(ctx context.Context, id string)
}

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

// startExpirySweep runs one expired-project sweep immediately, then repeats every
// interval until ctx is cancelled. Each pass removes every project whose expiry
// has passed from Postgres (the source of truth), drops its filestore bytes, and
// publishes a deleted event so connected clients refresh their lists. A zero (or
// negative) interval disables the sweep entirely — expired projects then linger
// in the store (there is no lazy per-request expiry check) until it is re-enabled.
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
