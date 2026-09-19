package main

import "context"

// Interface seams over *store.Store / *filestore.Store so the sweep is testable
// without Postgres or a real filestore. Behavior is unchanged.
type expiredProjectDeleter interface {
	DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error)
}

// projectDropper is the tail of service.ProjectService.Delete: drop a removed project's bytes and announce
// it. Shared so the sweep and DELETE /projects/{id} cannot drift apart.
type projectDropper interface {
	Dropped(ctx context.Context, id string)
}
