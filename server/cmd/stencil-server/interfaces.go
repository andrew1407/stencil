package main

import "context"

// Interface seams over *store.Store and the services, so the sweeps are testable without Postgres or a
// real filestore.
type expiredProjectDeleter interface {
	DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error)
}

type expiredSessionDeleter interface {
	DeleteExpiredSessions(ctx context.Context, now int64, limit int) (int, error)
}

type projectExister interface {
	ProjectExists(ctx context.Context, id string) (bool, error)
}

// projectDropper is the tail of service.ProjectService.Delete: drop a removed project's bytes and announce
// it. Shared so the sweep and DELETE /projects/{id} cannot drift apart.
type projectDropper interface {
	Dropped(ctx context.Context, id string)
}

// presenceBeater publishes this instance's live projects for the others (service.Presence).
type presenceBeater interface {
	Beat(ctx context.Context) error
}
