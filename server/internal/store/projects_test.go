// DB-gated: project CRUD, the last-writer-wins guard, and the patch columns
// that distinguish "absent" from "cleared". requireStore lives in store_test.go.
package store

import (
	"context"
	"encoding/json"
	"errors"
	"testing"

	"stencil/server/internal/protocol"
)

func TestProjectCRUDAndList(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()

	a, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Alpha", Source: "http://x/a.png", Color: "#ff8800"})
	if err != nil {
		t.Fatal(err)
	}
	if a.ID == "" || a.Version != 0 || a.Name != "Alpha" {
		t.Fatalf("bad created project: %+v", a)
	}
	if a.Color != "#ff8800" {
		t.Fatalf("color not persisted on create: %q", a.Color)
	}
	b, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Beta"})

	list, err := s.ListProjects(ctx, ProjectPage{})
	if err != nil {
		t.Fatal(err)
	}
	if len(list) != 2 {
		t.Fatalf("want 2 projects, got %d", len(list))
	}
	// Newest first; same-millisecond creates tie (ordering: projectlist_test.go).
	if list[0].UpdatedAt < list[1].UpdatedAt || (list[0].ID != b.ID && list[1].ID != b.ID) {
		t.Fatalf("list not ordered updated_at desc")
	}

	got, err := s.GetProject(ctx, a.ID)
	if err != nil || got.Source != "http://x/a.png" || got.Color != "#ff8800" {
		t.Fatalf("get project: %v %+v", err, got)
	}
	if _, err := s.GetProject(ctx, "p_missing_x"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing project should be ErrNotFound, got %v", err)
	}

	if err := s.DeleteProject(ctx, a.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := s.GetProject(ctx, a.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("deleted project should be gone")
	}
}

func TestUpdateProjectLWW(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "P"})

	layout := json.RawMessage(`{"lines":[{"x":1}]}`)
	upd, err := s.UpdateProject(ctx, p.ID, ProjectPatch{Layout: layout}, p.Version)
	if err != nil {
		t.Fatalf("update: %v", err)
	}
	if upd.Version != 1 {
		t.Fatalf("version not bumped: %d", upd.Version)
	}
	// jsonb normalizes whitespace, so compare semantically, not byte-for-byte.
	if !sameJSON(t, upd.Layout, layout) {
		t.Fatalf("layout not persisted: %s", upd.Layout)
	}
	// nil color leaves it unchanged (still empty here).
	if upd.Color != "" {
		t.Fatalf("nil color should not set a value, got %q", upd.Color)
	}
	// Stale version is rejected.
	if _, err := s.UpdateProject(ctx, p.ID, ProjectPatch{Layout: layout}, 0); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale update should conflict, got %v", err)
	}
	// Unknown project is not-found, not conflict.
	if _, err := s.UpdateProject(ctx, "p_missing_x", ProjectPatch{Layout: layout}, 0); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing update should be not-found, got %v", err)
	}
}

func TestSetFile(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Img"})

	upd, err := s.SetFile(ctx, p.ID, protocol.KindOriginal, "projects/"+p.ID+"/original.png", 640, 480)
	if err != nil {
		t.Fatal(err)
	}
	if !upd.HasImage || upd.ImageW != 640 || upd.ImageH != 480 {
		t.Fatalf("original file metadata not set: %+v", upd)
	}
	if upd.Version != 1 {
		t.Fatalf("version not bumped on setfile")
	}
	res, err := s.SetFile(ctx, p.ID, protocol.KindResult, "projects/"+p.ID+"/result.png", 0, 0)
	if err != nil || res.ResultPath == "" {
		t.Fatalf("result file not set: %v %+v", err, res)
	}
}
