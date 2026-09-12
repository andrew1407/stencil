package store

// DB-gated: project expiry stamping and the sweep that reaps it.

import (
	"context"
	"errors"
	"testing"

	"stencil/server/internal/protocol"
)

// TestCreateProjectExpiry checks an explicit create-time expiry round-trips, and
// that a project without one defaults to 0 ("keep forever").
func TestCreateProjectExpiry(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()

	withExp, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Exp", ExpiresAt: 5_000})
	if err != nil {
		t.Fatal(err)
	}
	if withExp.ExpiresAt != 5_000 {
		t.Fatalf("create-time expiry not persisted: %d", withExp.ExpiresAt)
	}
	// Re-fetch to confirm it scans back (not just the RETURNING row).
	got, err := s.GetProject(ctx, withExp.ID)
	if err != nil || got.ExpiresAt != 5_000 {
		t.Fatalf("expiry not read back: %v %d", err, got.ExpiresAt)
	}

	none, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "NoExp"})
	if none.ExpiresAt != 0 {
		t.Fatalf("default expiry should be 0 (never), got %d", none.ExpiresAt)
	}
}

// TestUpdateProjectExpiry checks the COALESCE expires_at path: a non-nil pointer
// sets it, nil leaves it untouched, and 0 clears it back to "keep forever".
func TestUpdateProjectExpiry(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "E"})

	exp := int64(9_000)
	upd, err := s.UpdateProject(ctx, p.ID, ProjectPatch{ExpiresAt: &exp}, p.Version)
	if err != nil || upd.ExpiresAt != 9_000 {
		t.Fatalf("set expiry: %v %d", err, upd.ExpiresAt)
	}
	// nil leaves it untouched while a name change bumps the version.
	name := "E2"
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Name: &name}, upd.Version)
	if err != nil || upd.ExpiresAt != 9_000 {
		t.Fatalf("nil expiry should be preserved: %v %d", err, upd.ExpiresAt)
	}
	// 0 explicitly clears the expiry (keep forever).
	zero := int64(0)
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{ExpiresAt: &zero}, upd.Version)
	if err != nil || upd.ExpiresAt != 0 {
		t.Fatalf("expiry should clear to 0: %v %d", err, upd.ExpiresAt)
	}
}

// TestDeleteExpiredProjects checks the sweep query: only projects with a non-zero
// expires_at at or before `now` are removed; it returns their ids.
func TestDeleteExpiredProjects(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()

	past, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Past", ExpiresAt: 1_000})
	future, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Future", ExpiresAt: 10_000})
	forever, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Forever"}) // expires_at 0

	ids, err := s.DeleteExpiredProjects(ctx, 5_000, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(ids) != 1 || ids[0] != past.ID {
		t.Fatalf("sweep should remove exactly the past-due project, got %v", ids)
	}
	if _, err := s.GetProject(ctx, past.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("swept project should be gone")
	}
	// The not-yet-expired and keep-forever projects survive.
	if _, err := s.GetProject(ctx, future.ID); err != nil {
		t.Fatalf("future project should survive: %v", err)
	}
	if _, err := s.GetProject(ctx, forever.ID); err != nil {
		t.Fatalf("keep-forever project should survive: %v", err)
	}
	// The boundary is inclusive: expires_at == now is swept.
	if ids2, _ := s.DeleteExpiredProjects(ctx, 10_000, 0); len(ids2) != 1 || ids2[0] != future.ID {
		t.Fatalf("expires_at == now should be swept, got %v", ids2)
	}
}
