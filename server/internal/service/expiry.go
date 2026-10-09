package service

import (
	"context"
	"time"
)

// Expiry is the sweep's half of the project lifecycle: which expired rows may go.
type Expiry struct {
	Store ExpiredStore
	// OpTimeout bounds each store call (OP_TIMEOUT_SECONDS); 0 leaves the caller's context alone.
	OpTimeout time.Duration
}

// DeleteExpiredProjects takes up to limit expired rows and returns their ids.
func (e Expiry) DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error) {
	dctx, cancel := withOpTimeout(ctx, e.OpTimeout)
	defer cancel()
	return e.Store.DeleteExpiredProjects(dctx, now, limit)
}
