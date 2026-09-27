package service

import "context"

// Expiry is the sweep's half of the project lifecycle: which expired rows may go. A project someone is
// editing live, here or on another instance, is deferred, as Delete refuses a shared session, and the
// next sweep retries it.
type Expiry struct {
	Store  ExpiredStore
	Live   LiveProjects // optional: nil defers nothing
	Remote RemoteLive   // optional: nil is a single instance
}

// DeleteExpiredProjects takes up to limit expired rows no live session is editing and returns their ids.
// An unreadable remote list sweeps nothing, since any row could be live elsewhere.
func (e Expiry) DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error) {
	var editing []string
	if e.Live != nil {
		editing = e.Live.LiveProjectIDs()
	}
	if e.Remote != nil {
		elsewhere, err := e.Remote.LiveElsewhere(ctx)
		if err != nil {
			return nil, err
		}
		editing = append(editing, elsewhere...)
	}
	return e.Store.DeleteExpiredProjects(ctx, now, limit, editing)
}
