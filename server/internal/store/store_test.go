package store

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

// requireStore connects to DATABASE_URL, migrates, and truncates, or skips the
// test when no database is configured/reachable. Mirrors the self-skipping e2e
// convention used by mcp/.
func requireStore(t *testing.T) *Store {
	t.Helper()
	url := os.Getenv("DATABASE_URL")
	if url == "" {
		t.Skip("DATABASE_URL not set; skipping Postgres integration test")
	}
	ctx := context.Background()
	s, err := New(ctx, url)
	if err != nil {
		t.Skipf("Postgres unreachable (%v); skipping", err)
	}
	if err := Migrate(ctx, s.MigratePool()); err != nil {
		t.Fatalf("migrate: %v", err)
	}
	if _, err := s.pool.Exec(ctx, `TRUNCATE projects, sessions CASCADE`); err != nil {
		t.Fatalf("truncate: %v", err)
	}
	t.Cleanup(s.Close)
	return s
}

func TestSessionLifecycle(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	_, hash, _ := auth.GenerateToken()
	sess, err := s.CreateSession(ctx, hash, "cli", 1000, 9999)
	if err != nil {
		t.Fatal(err)
	}
	got, err := s.ResolveToken(ctx, hash)
	if err != nil {
		t.Fatalf("resolve: %v", err)
	}
	if got.ID != sess.ID || got.ExpiresAt != 9999 {
		t.Fatalf("resolved session mismatch: %+v", got)
	}
	if err := s.DeleteSession(ctx, sess.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := s.ResolveToken(ctx, hash); !errors.Is(err, auth.ErrInvalidToken) {
		t.Fatalf("revoked token should be invalid, got %v", err)
	}
}

func TestProjectCRUDAndList(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()

	a, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Alpha", Source: "http://x/a.png"})
	if err != nil {
		t.Fatal(err)
	}
	if a.ID == "" || a.Version != 0 || a.Name != "Alpha" {
		t.Fatalf("bad created project: %+v", a)
	}
	b, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Beta"})

	list, err := s.ListProjects(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if len(list) != 2 {
		t.Fatalf("want 2 projects, got %d", len(list))
	}
	// Newest-updated first: Beta was created after Alpha.
	if list[0].ID != b.ID {
		t.Fatalf("list not ordered updated_at desc")
	}

	got, err := s.GetProject(ctx, a.ID)
	if err != nil || got.Source != "http://x/a.png" {
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
	upd, err := s.UpdateProject(ctx, p.ID, nil, layout, p.Version)
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
	// Stale version is rejected.
	if _, err := s.UpdateProject(ctx, p.ID, nil, layout, 0); !errors.Is(err, ErrConflict) {
		t.Fatalf("stale update should conflict, got %v", err)
	}
	// Unknown project is not-found, not conflict.
	if _, err := s.UpdateProject(ctx, "p_missing_x", nil, layout, 0); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing update should be not-found, got %v", err)
	}
}

// sameJSON reports whether two JSON payloads are semantically equal, ignoring
// whitespace and key ordering (jsonb does not preserve either).
func sameJSON(t *testing.T, a, b []byte) bool {
	t.Helper()
	var av, bv any
	if err := json.Unmarshal(a, &av); err != nil {
		t.Fatalf("unmarshal a: %v", err)
	}
	if err := json.Unmarshal(b, &bv); err != nil {
		t.Fatalf("unmarshal b: %v", err)
	}
	am, _ := json.Marshal(av)
	bm, _ := json.Marshal(bv)
	return string(am) == string(bm)
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
