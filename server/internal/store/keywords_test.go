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
	if joinKeywords([]string{"a", " a ", "b"}) != "a\nb" {
		t.Fatal("the legacy column must carry the normalized list")
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
	// The legacy column is still written for the rollback window.
	var legacy string
	if err := s.pool.QueryRow(ctx, `SELECT keywords FROM projects WHERE id = $1`, p.ID).Scan(&legacy); err != nil {
		t.Fatal(err)
	}
	if legacy != "maps\nOcean" {
		t.Fatalf("legacy keywords column: %q", legacy)
	}
}

// DB-gated: 0002's back-fill lifts a pre-migration row's newline blob into the
// array, and re-running the migration leaves a cleared row cleared.
func TestKeywordsBackfillFromLegacyColumn(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	if _, err := s.pool.Exec(ctx,
		`INSERT INTO projects (id, name, created_at, updated_at, keywords, keywords_arr)
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
