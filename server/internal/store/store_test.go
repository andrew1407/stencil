package store

import (
	"context"
	"os"
	"testing"
)

// requireStore connects to TEST_DATABASE_URL, migrates, and truncates, or skips
// the test when no database is configured/reachable. Mirrors the self-skipping
// e2e convention used by mcp/. Deliberately NOT DATABASE_URL: the truncate below
// wipes the named database, and that variable points at the LIVE server's.
func requireStore(t *testing.T) *Store {
	t.Helper()
	url := os.Getenv("TEST_DATABASE_URL")
	if url == "" {
		t.Skip("TEST_DATABASE_URL not set; skipping Postgres integration test (never runs against DATABASE_URL — the setup truncates)")
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
