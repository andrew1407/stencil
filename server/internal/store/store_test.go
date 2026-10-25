package store

import (
	"context"
	"os"
	"testing"
)

// requireStore connects to TEST_DATABASE_URL, migrates and truncates; skips only when it is unset, so a set but
// unreachable database fails. Deliberately NOT DATABASE_URL: the truncate wipes the database it names.
func requireStore(t testing.TB) *Store {
	t.Helper()
	url := os.Getenv("TEST_DATABASE_URL")
	if url == "" {
		t.Skip("TEST_DATABASE_URL not set; skipping Postgres integration test (never runs against DATABASE_URL — the setup truncates)")
	}
	ctx := context.Background()
	s, err := New(ctx, url)
	if err != nil {
		t.Fatalf("TEST_DATABASE_URL is set but Postgres is unreachable: %v", err)
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

// RequireTestStore is requireStore for the external store_test package, which drives the store through
// the services and the REST routes above it.
func RequireTestStore(t testing.TB) *Store { return requireStore(t) }
