package store

// DB-gated: original_hash (0005) is set by an original's SetFile, replaced by the next one, left alone by
// a result, read by every metadata column set, and NULL (read as "") on rows stored before it.

import (
	"context"
	"testing"

	"stencil/server/internal/protocol"
)

func TestSetFileRecordsTheOriginalsHash(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Hashed"})
	if p.OriginalHash != "" {
		t.Fatalf("a new project carries hash %q", p.OriginalHash)
	}
	original := StoredFile{Kind: protocol.KindOriginal, Path: "projects/" + p.ID + "/original.png", W: 8, H: 8, Hash: "aa11"}
	if rec, err := s.SetFile(ctx, p.ID, original); err != nil || rec.OriginalHash != "aa11" {
		t.Fatalf("SetFile original: %v %+v", err, rec)
	}
	original.Hash = "bb22" // same path, size and type: a replaced picture
	if rec, _ := s.SetFile(ctx, p.ID, original); rec.OriginalHash != "bb22" {
		t.Fatalf("a replaced original kept hash %q", rec.OriginalHash)
	}
	if rec, _ := s.SetFile(ctx, p.ID, StoredFile{Kind: protocol.KindResult, Path: "projects/" + p.ID + "/result.png"}); rec.OriginalHash != "bb22" {
		t.Fatalf("a result moved the original's hash to %q", rec.OriginalHash)
	}
	list, _ := s.ListProjects(ctx, ProjectPage{})
	full, _ := s.GetProject(ctx, p.ID)
	snap, _ := s.GetProject(ctx, p.ID)
	if len(list) != 1 || list[0].OriginalHash != "bb22" || full.OriginalHash != "bb22" || snap.OriginalHash != "bb22" {
		t.Fatalf("reads lost the hash: list %+v, get %q, again %q", list, full.OriginalHash, snap.OriginalHash)
	}
	original.Hash = "" // an original stored without a hash is unknown again, never the old picture's
	if rec, _ := s.SetFile(ctx, p.ID, original); rec.OriginalHash != "" {
		t.Fatalf("an unhashed original kept hash %q", rec.OriginalHash)
	}
	var isNull bool
	if err := s.pool.QueryRow(ctx, `SELECT original_hash IS NULL FROM projects WHERE id = $1`, p.ID).Scan(&isNull); err != nil || !isNull {
		t.Fatalf("an unknown hash is stored as NULL: %v %v", err, isNull)
	}
}

// Upgrading a database: a row stored before 0005 gains the column as NULL, never a guessed hash.
func TestOriginalHashMigrationLeavesOldRowsUnknown(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	if _, err := s.pool.Exec(ctx, `ALTER TABLE projects DROP COLUMN original_hash;
		DELETE FROM schema_migrations WHERE version = '0005_original_hash.sql';
		INSERT INTO projects (id, name, created_at, updated_at, has_image, original_path)
		VALUES ('p_old_a', 'Old', 1, 1, true, 'projects/p_old_a/original.png')`); err != nil {
		t.Fatal(err)
	}
	if err := Migrate(ctx, s.MigratePool()); err != nil {
		t.Fatal(err)
	}
	if rec, err := s.GetProjectMeta(ctx, "p_old_a"); err != nil || !rec.HasImage || rec.OriginalHash != "" {
		t.Fatalf("an old row after 0005: %v %+v", err, rec)
	}
}
