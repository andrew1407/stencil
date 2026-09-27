package main

import (
	"context"
	"log"
	"sync"
	"time"
)

// maintenance is what one expiry pass drives: expired projects in batches (their bytes dropped over a
// bounded pool of workers), then expired session rows. A nil sessions skips that half.
type maintenance struct {
	projects expiredProjectDeleter
	drops    projectDropper
	sessions expiredSessionDeleter
	batch    int // rows one DELETE ... RETURNING takes (SWEEP_BATCH)
	workers  int // concurrent byte drops (SWEEP_WORKERS)
}

// dropEach drops each swept project over a bounded pool: thousands of ids should
// not run one-at-a-time, nor spawn one goroutine each.
func (m maintenance) dropEach(ctx context.Context, ids []string) {
	work := make(chan string)
	var wg sync.WaitGroup
	for i := 0; i < m.workers && i < len(ids); i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for id := range work {
				m.drops.Dropped(ctx, id)
			}
		}()
	}
	for _, id := range ids {
		work <- id
	}
	close(work)
	wg.Wait()
}

// pass runs one sweep. Each half takes a batch per DB round trip and keeps going while a pass comes back
// full, so a large backlog still clears in one sweep without an unbounded delete.
func (m maintenance) pass(ctx context.Context) {
	projects := 0
	for {
		ids, err := m.projects.DeleteExpiredProjects(ctx, time.Now().UnixMilli(), m.batch)
		if err != nil {
			log.Printf("expiry sweep: %v", err)
			break
		}
		m.dropEach(ctx, ids)
		projects += len(ids)
		if len(ids) < m.batch {
			break
		}
	}
	if projects > 0 {
		log.Printf("expiry sweep: removed %d expired project(s)", projects)
	}
	if m.sessions == nil {
		return
	}
	sessions := 0
	for {
		n, err := m.sessions.DeleteExpiredSessions(ctx, time.Now().UnixMilli(), m.batch)
		if err != nil {
			log.Printf("expiry sweep: sessions: %v", err)
			break
		}
		if sessions += n; n < m.batch {
			break
		}
	}
	if sessions > 0 {
		log.Printf("expiry sweep: removed %d expired session(s)", sessions)
	}
}

// startExpirySweep sweeps once, then every interval until ctx is cancelled, dropping filestore bytes and
// publishing a deleted event per project. A zero or negative interval disables it (no lazy expiry).
func startExpirySweep(ctx context.Context, wg *sync.WaitGroup, m maintenance, interval time.Duration) {
	if interval <= 0 {
		log.Printf("expiry sweep disabled (EXPIRY_SWEEP_MINUTES=0)")
		return
	}
	every(ctx, wg, interval, m.pass)
}

// every runs fn now and then on each tick until ctx is cancelled, on a goroutine run() joins through wg
// before it closes the store and bus.
func every(ctx context.Context, wg *sync.WaitGroup, interval time.Duration, fn func(context.Context)) {
	wg.Add(1)
	go func() {
		defer wg.Done()
		fn(ctx)
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				fn(ctx)
			}
		}
	}()
}
