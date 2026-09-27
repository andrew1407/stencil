package main

import (
	"context"
	"log"
	"sync"
	"time"

	"stencil/server/internal/config"
)

// startPresence publishes this instance's live projects now, per heartbeat, and once a burst of joins and
// leaves settles. Rows outlive exit by the TTL, which covers this instance's editors reconnecting to another.
func startPresence(ctx context.Context, wg *sync.WaitGroup, p presenceBeater, changed <-chan struct{}, opts config.PresenceOptions, opTimeout time.Duration) {
	wg.Add(1)
	go func() {
		defer wg.Done()
		ticker := time.NewTicker(opts.Heartbeat)
		defer ticker.Stop()
		for {
			beatOnce(ctx, p, opTimeout)
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
			case <-changed:
				select {
				case <-ctx.Done():
					return
				case <-time.After(opts.Settle):
				}
			}
		}
	}()
}

func beatOnce(ctx context.Context, p presenceBeater, opTimeout time.Duration) {
	ctx, cancel := context.WithTimeout(ctx, opTimeout)
	defer cancel()
	if err := p.Beat(ctx); err != nil && ctx.Err() == nil {
		log.Printf("presence heartbeat: %v", err)
	}
}
