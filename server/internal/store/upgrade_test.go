package store_test

// DB-gated, end to end: an old-schema database whose originals live only in original_content upgrades
// through the boot's mover into the filestore, and the files route serves what it moved.

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
)

func TestUpgradeServesInlineOriginalsFromTheFilesRoute(t *testing.T) {
	s := store.RequireTestStore(t)
	ctx := context.Background()
	pool := s.MigratePool()
	img := []byte("\x89PNG\r\n\x1a\nthe server never decodes these")
	if _, err := pool.Exec(ctx, `ALTER TABLE projects ADD COLUMN original_content text NOT NULL DEFAULT '';
		DROP TABLE file_charges; DELETE FROM schema_migrations WHERE version >= '0006'`); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = pool.Exec(ctx, `TRUNCATE projects CASCADE; ALTER TABLE projects DROP COLUMN IF EXISTS original_content`)
		_ = store.Migrate(ctx, pool)
	})
	for id, content := range map[string]string{
		"p_up_a": "data:image/png;base64," + base64.StdEncoding.EncodeToString(img),
		"p_up_b": "data:text/plain;base64,aGk=",
	} {
		if _, err := pool.Exec(ctx, `INSERT INTO projects (id, name, created_at, updated_at, has_image, image_w, image_h, original_content)
			VALUES ($1, 'Old', 1, 1, true, 4, 3, $2)`, id, content); err != nil {
			t.Fatal(err)
		}
	}
	root := t.TempDir()
	raw, err := filestore.New(root)
	if err != nil {
		t.Fatal(err)
	}
	if err := store.MigrateWith(ctx, pool, service.LegacyOriginals(raw)); err != nil {
		t.Fatal(err)
	}
	onDisk, err := os.ReadFile(filepath.Join(root, "projects", "p_up_a", "original.png"))
	if err != nil || string(onDisk) != string(img) {
		t.Fatalf("moved bytes: %v %q", err, onDisk)
	}
	sum := sha256.Sum256(img)
	if a, _ := s.GetProjectMeta(ctx, "p_up_a"); a.OriginalPath != "projects/p_up_a/original.png" ||
		a.OriginalHash != hex.EncodeToString(sum[:]) || a.ImageW != 4 {
		t.Fatalf("the moved row: %+v", a)
	}
	serve := restOver(t, s, root)
	if code, body, typ := serve("/projects/p_up_a/files/original"); code != http.StatusOK || body != string(img) || typ != "image/png" {
		t.Fatalf("files route: %d %q %s", code, body, typ)
	}
	if code, body, _ := serve("/projects/p_up_a"); code != http.StatusOK || strings.Contains(body, "originalContent") {
		t.Fatalf("GET /projects/{id}: %d %s", code, body)
	}
	if code, _, _ := serve("/projects/p_up_b/files/original"); code != http.StatusNotFound {
		t.Fatalf("an undecodable inline original is dropped, not served: %d", code)
	}
	// A second boot moves nothing and re-applies nothing.
	if err := store.MigrateWith(ctx, pool, func(context.Context, store.LegacyOriginal) (store.StoredFile, error) {
		t.Fatal("a second boot moved an original")
		return store.StoredFile{}, nil
	}); err != nil {
		t.Fatal(err)
	}
}

// restOver serves GET requests through the REST API over the real store and a filestore at root.
func restOver(t *testing.T, s *store.Store, root string) func(path string) (int, string, string) {
	t.Helper()
	fs, err := filestore.New(root)
	if err != nil {
		t.Fatal(err)
	}
	const admin = "upgrade-admin"
	api := httpapi.New(httpapi.Deps{Projects: s, Sessions: s, Files: fs, Charges: s, Bus: eventbus.NewInProc(), AdminToken: admin})
	req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
	req.Header.Set("X-Admin-Token", admin)
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	var tok protocol.TokenResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &tok); err != nil || tok.Token == "" {
		t.Fatalf("token: %v %s", err, rec.Body.String())
	}
	return func(path string) (int, string, string) {
		req := httptest.NewRequest(http.MethodGet, path, nil)
		req.Header.Set("Authorization", "Bearer "+tok.Token)
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec.Code, rec.Body.String(), rec.Header().Get("Content-Type")
	}
}
