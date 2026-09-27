package store

// DB-gated: the 0006 back-fill moves each inline original before the column goes, in one transaction a
// crash rolls back whole, and never drops a column that still holds one.

import (
	"context"
	"errors"
	"strings"
	"testing"
)

// oldSchema puts the pre-0006 column back, with rows (id → content, original_path), and forgets 0006.
func oldSchema(t *testing.T, s *Store, rows map[string][2]string) {
	t.Helper()
	ctx := context.Background()
	if _, err := s.pool.Exec(ctx, `ALTER TABLE projects ADD COLUMN IF NOT EXISTS original_content text NOT NULL DEFAULT '';
		DELETE FROM schema_migrations WHERE version LIKE '0006%'`); err != nil {
		t.Fatal(err)
	}
	for id, r := range rows {
		if _, err := s.pool.Exec(ctx, `INSERT INTO projects (id, name, created_at, updated_at, has_image, original_content, original_path)
			VALUES ($1, 'Old', 1, 1, true, $2, $3)`, id, r[0], r[1]); err != nil {
			t.Fatal(err)
		}
	}
	t.Cleanup(func() { // a failed test must not leave the column for the next one
		_, _ = s.pool.Exec(ctx, `TRUNCATE projects CASCADE; ALTER TABLE projects DROP COLUMN IF EXISTS original_content`)
		_ = Migrate(ctx, s.pool)
	})
}

func hasContentColumn(t *testing.T, s *Store) bool {
	t.Helper()
	var present bool
	if err := s.pool.QueryRow(context.Background(), `SELECT EXISTS (SELECT 1 FROM information_schema.columns
		WHERE table_schema = current_schema() AND table_name = 'projects' AND column_name = 'original_content')`).
		Scan(&present); err != nil {
		t.Fatal(err)
	}
	return present
}

// fakeMove stores "good-*" content and calls anything else undecodable, recording each row it saw.
func fakeMove(seen *[]string) MoveOriginal {
	return func(_ context.Context, row LegacyOriginal) (StoredFile, error) {
		*seen = append(*seen, row.ID)
		if !strings.HasPrefix(row.Content, "good-") {
			return StoredFile{}, ErrUndecodable
		}
		return StoredFile{Path: "projects/" + row.ID + "/original.png", Hash: "h-" + row.Content}, nil
	}
}

func TestBackfillMovesInlineOriginalsThenDropsTheColumn(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	oldSchema(t, s, map[string][2]string{
		"p_leg_a": {"good-a", ""},
		"p_leg_b": {"junk!", ""},
		"p_leg_c": {"good-c", "projects/p_leg_c/original.jpg"}, // uploaded since: the file wins
		"p_leg_d": {"", ""},
	})
	var seen []string
	if err := MigrateWith(ctx, s.pool, fakeMove(&seen)); err != nil {
		t.Fatal(err)
	}
	if len(seen) != 2 || seen[0] != "p_leg_a" || seen[1] != "p_leg_b" {
		t.Fatalf("moved %v, want the two rows with content and no upload, in id order", seen)
	}
	a, _ := s.GetProjectMeta(ctx, "p_leg_a")
	if a.OriginalPath != "projects/p_leg_a/original.png" || a.OriginalHash != "h-good-a" || a.Version != 0 || a.UpdatedAt != 1 {
		t.Fatalf("a moved original: %+v, want its path and hash, version and updated_at untouched", a)
	}
	if b, _ := s.GetProjectMeta(ctx, "p_leg_b"); b.OriginalPath != "" || !b.HasImage {
		t.Fatalf("an undecodable original: %+v, want the row kept without one", b)
	}
	if c, _ := s.GetProjectMeta(ctx, "p_leg_c"); c.OriginalPath != "projects/p_leg_c/original.jpg" || c.OriginalHash != "" {
		t.Fatalf("an uploaded original was touched: %+v", c)
	}
	if hasContentColumn(t, s) {
		t.Fatal("original_content survived 0006")
	}
	ledger := recordedMigrations(t, s)
	for _, v := range []string{originalBackfill, "0006_original_content_drop.sql"} {
		if _, ok := ledger[v]; !ok {
			t.Fatalf("%s not recorded: %v", v, ledger)
		}
	}
	// A second boot is a no-op: nothing to move, nothing re-applied.
	if err := MigrateWith(ctx, s.pool, func(context.Context, LegacyOriginal) (StoredFile, error) {
		t.Fatal("a second boot moved an original")
		return StoredFile{}, nil
	}); err != nil {
		t.Fatal(err)
	}
}

// A mover that fails mid-way (a crash, a full disk) rolls the whole upgrade back: every row keeps its
// content and no path, the column stays, and the next boot moves them all.
func TestBackfillFailureKeepsTheColumnAndRerunsCleanly(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	oldSchema(t, s, map[string][2]string{"p_leg_a": {"good-a", ""}, "p_leg_b": {"good-b", ""}})
	var seen []string
	good, boom := fakeMove(&seen), errors.New("disk full")
	failing := func(ctx context.Context, row LegacyOriginal) (StoredFile, error) {
		if row.ID == "p_leg_b" {
			return StoredFile{}, boom
		}
		return good(ctx, row)
	}
	if err := MigrateWith(ctx, s.pool, failing); !errors.Is(err, boom) {
		t.Fatalf("a failed move: %v, want it passed through", err)
	}
	if !hasContentColumn(t, s) {
		t.Fatal("the column was dropped although an original was not moved")
	}
	if a, _ := s.GetProjectMeta(ctx, "p_leg_a"); a.OriginalPath != "" {
		t.Fatalf("a row moved before the failure kept %q; the transaction must roll it back", a.OriginalPath)
	}
	if _, ok := recordedMigrations(t, s)[originalBackfill]; ok {
		t.Fatal("a failed back-fill was recorded")
	}
	if err := MigrateWith(ctx, s.pool, good); err != nil {
		t.Fatal(err)
	}
	for _, id := range []string{"p_leg_a", "p_leg_b"} {
		if p, _ := s.GetProjectMeta(ctx, id); p.OriginalPath == "" {
			t.Fatalf("%s not moved on the re-run", id)
		}
	}
	if hasContentColumn(t, s) {
		t.Fatal("original_content survived the re-run")
	}
}

// Without a mover the upgrade refuses, and the drop's own guard refuses a column still holding one.
func TestContentIsNeverDroppedUnmoved(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	oldSchema(t, s, map[string][2]string{"p_leg_a": {"good-a", ""}})
	if err := Migrate(ctx, s.pool); err == nil {
		t.Fatal("a database holding an inline original migrated without a mover")
	}
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer tx.Rollback(ctx)
	if err := applySQL(ctx, tx, "0006_original_content_drop.sql"); err == nil {
		t.Fatal("the drop ran over an original no file replaced")
	}
}
