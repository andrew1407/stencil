package service

import (
	"context"
	"time"
)

// Expiry is the sweep's half of the project lifecycle: which expired rows may go. A project someone is
// editing live, here or on another instance, is deferred, as Delete refuses a shared session, and the
// next sweep retries it.
type Expiry struct {
	Store  ExpiredStore
	Live   LiveProjects // optional: nil defers nothing
	Remote RemoteLive   // optional: nil is a single instance
	// OpTimeout bounds each store call (OP_TIMEOUT_SECONDS); 0 leaves the caller's context alone.
	OpTimeout time.Duration
}

// DeleteExpiredProjects takes up to limit expired rows no live session is editing and returns their ids.
// An unreadable remote list sweeps nothing, since any row could be live elsewhere.
func (e Expiry) DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error) {
	var editing []string
	if e.Live != nil {
		editing = e.Live.LiveProjectIDs()
	}
	if e.Remote != nil {
		rctx, cancel := withOpTimeout(ctx, e.OpTimeout)
		elsewhere, err := e.Remote.LiveElsewhere(rctx)
		cancel()
		if err != nil {
			return nil, err
		}
		editing = append(editing, elsewhere...)
	}
	dctx, cancel := withOpTimeout(ctx, e.OpTimeout)
	defer cancel()
	return e.Store.DeleteExpiredProjects(dctx, now, limit, editing)
}
