package service

// Project lifecycle policy: what a project must be to exist at all, how long it
// lives, and who is allowed to remove it.

import (
	"context"
	"time"

	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// ProjectService owns project creation and deletion.
type ProjectService struct {
	Projects ProjectStore
	Files    ProjectFiles   // optional: nil skips the byte drop
	Live     SessionCounter // optional: nil means no live session anywhere
	Bus      eventbus.Bus
	TTL      time.Duration // default project lifetime; 0 = no expiry (off)
}

// NewProjects builds the service. Files and live may be nil.
func NewProjects(projects ProjectStore, files ProjectFiles, live SessionCounter, b eventbus.Bus, ttl time.Duration) *ProjectService {
	return &ProjectService{Projects: projects, Files: files, Live: live, Bus: b, TTL: ttl}
}

// Create persists a new project and announces it. Both rules are policy, not transport: a project is
// created FROM an image (HasImage), and one naming no expiry inherits PROJECT_TTL (off by default).
func (s *ProjectService) Create(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	if !req.HasImage {
		return protocol.ProjectRecord{}, ErrImageRequired
	}
	if req.ExpiresAt == 0 && s.TTL > 0 {
		req.ExpiresAt = clock.NowMs() + s.TTL.Milliseconds()
	}
	rec, err := s.Projects.CreateProject(ctx, ownerSession, req)
	if err != nil {
		return protocol.ProjectRecord{}, err
	}
	announce(ctx, s.Bus, protocol.EventCreated, rec)
	return rec, nil
}

// Delete removes a project row, then its bytes, then announces it. A project is a shared workspace, so
// deletion is allowed only while at most one client is in its live edit session.
func (s *ProjectService) Delete(ctx context.Context, id string) error {
	if s.Live != nil && s.Live.ConnectionCount(id) >= 2 {
		return ErrProjectInUse
	}
	if err := s.Projects.DeleteProject(ctx, id); err != nil {
		return err
	}
	s.Dropped(ctx, id)
	return nil
}

// Dropped is Delete's tail: drop a removed project's bytes and tell connected clients. The expiry sweep
// calls it directly — its DELETE ... RETURNING has already taken the rows.
func (s *ProjectService) Dropped(ctx context.Context, id string) {
	if s.Files != nil {
		_ = s.Files.Remove(id) // best-effort; a missing dir is not an error
	}
	announce(ctx, s.Bus, protocol.EventDeleted, protocol.ProjectRecord{ID: id})
}
