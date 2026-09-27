package store

import (
	"context"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

func TestNormalizeKeywords(t *testing.T) {
	got := normalizeKeywords([]string{" alpha ", "", "Beta", "ALPHA", "beta", "gamma"})
	if strings.Join(got, ",") != "alpha,Beta,gamma" {
		t.Fatalf("normalize: %v", got)
	}
	// Never nil: a nil slice encodes as SQL NULL, which UPDATE reads as "unchanged".
	if got := normalizeKeywords(nil); got == nil || len(got) != 0 {
		t.Fatalf("empty input must yield an empty, non-nil slice: %#v", got)
	}
}

// DB-gated (TEST_DATABASE_URL): keywords round-trip through the text[] column and
// are searchable there — the point of migration 0002.
func TestKeywordsArrayRoundTripAndSearch(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{
		Name: "K", Keywords: []string{"maps", "Ocean", "maps"}})
	if err != nil {
		t.Fatal(err)
	}
	if strings.Join(p.Keywords, ",") != "maps,Ocean" {
		t.Fatalf("create keywords: %v", p.Keywords)
	}
	got, err := s.GetProject(ctx, p.ID)
	if err != nil || strings.Join(got.Keywords, ",") != "maps,Ocean" {
		t.Fatalf("read back: %v %v", err, got.Keywords)
	}
	// The array column is queryable (the newline blob was not).
	var n int
	if err := s.pool.QueryRow(ctx,
		`SELECT count(*) FROM projects WHERE keywords_arr @> ARRAY['Ocean']`).Scan(&n); err != nil {
		t.Fatal(err)
	}
	if n != 1 {
		t.Fatalf("keyword search found %d rows, want 1", n)
	}
	// 0003 dropped the legacy newline blob once nothing read it.
	var legacy int
	if err := s.pool.QueryRow(ctx, `SELECT count(*) FROM information_schema.columns
		WHERE table_name = 'projects' AND column_name = 'keywords'`).Scan(&legacy); err != nil {
		t.Fatal(err)
	}
	if legacy != 0 {
		t.Fatal("the legacy keywords column survived migration 0003")
	}
}

// DB-gated: upgrading a pre-0002 database, the back-fill lifts a row's newline blob into the array
// before 0003 drops the blob, and a later migrate leaves a cleared row cleared.
func TestKeywordsBackfillFromLegacyColumn(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	if _, err := s.pool.Exec(ctx, `
		ALTER TABLE projects ADD COLUMN IF NOT EXISTS keywords text NOT NULL DEFAULT '';
		DELETE FROM schema_migrations WHERE version >= '0002';
		INSERT INTO projects (id, name, created_at, updated_at, keywords, keywords_arr)
		VALUES ('p_legacy_a', 'Old', 1, 1, 'alpha'||chr(10)||'Beta', '{}')`); err != nil {
		t.Fatal(err)
	}
	if err := Migrate(ctx, s.MigratePool()); err != nil {
		t.Fatalf("re-migrate: %v", err)
	}
	got, err := s.GetProject(ctx, "p_legacy_a")
	if err != nil || strings.Join(got.Keywords, ",") != "alpha,Beta" {
		t.Fatalf("back-fill: %v %v", err, got.Keywords)
	}
	// Clearing keywords clears both columns, and the back-fill does not resurrect them.
	empty := make([]string, 0)
	if _, err := s.UpdateProject(ctx, "p_legacy_a", ProjectPatch{Keywords: &empty}, got.Version); err != nil {
		t.Fatal(err)
	}
	if err := Migrate(ctx, s.MigratePool()); err != nil {
		t.Fatal(err)
	}
	if got, _ := s.GetProject(ctx, "p_legacy_a"); len(got.Keywords) != 0 {
		t.Fatalf("cleared keywords came back: %v", got.Keywords)
	}
}
