package service

// Project lifecycle policy: what a project must be to exist at all, how long it
// lives, and the order a removal runs in.

import (
	"context"
	"log"
	"time"

	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// ProjectService owns project creation and deletion.
type ProjectService struct {
	Projects  ProjectStore
	Files     ProjectFiles // optional: nil skips the byte drop
	Bus       eventbus.Bus
	TTL       time.Duration // default project lifetime; 0 = no expiry (off)
	Originals *FileService  // stores a create's inline original; nil refuses one
}

// NewProjects builds the service. Files may be nil.
func NewProjects(projects ProjectStore, files ProjectFiles, b eventbus.Bus, ttl time.Duration) *ProjectService {
	return &ProjectService{Projects: projects, Files: files, Bus: b, TTL: ttl}
}

// Create persists and announces a project: one made FROM an image (HasImage), inheriting PROJECT_TTL when it
// names no expiry, its inline original stored as the file, charged to the creator, before the announcement.
func (s *ProjectService) Create(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	if !req.HasImage {
		return protocol.ProjectRecord{}, ErrImageRequired
	}
	var inline *inlineOriginal
	if req.OriginalContent != "" {
		o, err := parseInlineOriginal(req.OriginalContent)
		if err != nil {
			return protocol.ProjectRecord{}, err
		}
		inline, req.OriginalContent = &o, ""
	}
	if req.ExpiresAt == 0 && s.TTL > 0 {
		req.ExpiresAt = clock.NowMs() + s.TTL.Milliseconds()
	}
	rec, err := s.Projects.CreateProject(ctx, ownerSession, req)
	if err != nil {
		return protocol.ProjectRecord{}, err
	}
	if inline != nil {
		if rec, err = s.storeInline(ctx, rec, *inline, ownerSession); err != nil {
			return protocol.ProjectRecord{}, err
		}
	}
	eventbus.PublishProjectEvent(ctx, s.Bus, protocol.EventCreated, rec)
	return rec, nil
}

// Delete removes a project row, then its bytes, then announces it.
func (s *ProjectService) Delete(ctx context.Context, id string) error {
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
		if err := s.Files.Remove(id); err != nil { // the reconcile pass retries what is left behind
			log.Printf("service: drop bytes of project %s: %v", id, err)
		}
	}
	eventbus.PublishProjectEvent(ctx, s.Bus, protocol.EventDeleted, protocol.ProjectRecord{ID: id})
}
