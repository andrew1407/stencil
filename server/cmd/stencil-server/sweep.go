package main

import (
	"context"
	"log"
	"sync"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
)

// Interface seams over *store.Store / *filestore.Store so the sweep is testable
// without Postgres or a real filestore. Behavior is unchanged.
type expiredProjectDeleter interface {
	DeleteExpiredProjects(ctx context.Context, now int64) ([]string, error)
}

type projectFileRemover interface {
	Remove(id string) error
}

// startExpirySweep runs one expired-project sweep immediately, then repeats every
// interval until ctx is cancelled. Each pass removes every project whose expiry
// has passed from Postgres (the source of truth), drops its filestore bytes, and
// publishes a deleted event so connected clients refresh their lists. A zero (or
// negative) interval disables the sweep entirely — expired projects then linger
// in the store (there is no lazy per-request expiry check) until it is re-enabled.
func startExpirySweep(ctx context.Context, wg *sync.WaitGroup, st expiredProjectDeleter, fs projectFileRemover, b bus.Bus, interval time.Duration) {
	if interval <= 0 {
		log.Printf("expiry sweep disabled (EXPIRY_SWEEP_MINUTES=0)")
		return
	}
	sweep := func() {
		ids, err := st.DeleteExpiredProjects(ctx, time.Now().UnixMilli())
		if err != nil {
			log.Printf("expiry sweep: %v", err)
			return
		}
		for _, id := range ids {
			if fs != nil {
				_ = fs.Remove(id) // best-effort; a missing dir is not an error
			}
			bus.PublishProjectEvent(ctx, b, protocol.EventDeleted, protocol.ProjectRecord{ID: id})
		}
		if len(ids) > 0 {
			log.Printf("expiry sweep: removed %d expired project(s)", len(ids))
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
