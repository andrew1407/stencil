package store

// DB-gated: the schema_migrations ledger and the advisory lock around it.

import (
	"context"
	"strings"
	"sync"
	"testing"
)

func recordedMigrations(t *testing.T, s *Store) map[string]int64 {
	t.Helper()
	rows, err := s.pool.Query(context.Background(), `SELECT version, applied_at FROM schema_migrations`)
	if err != nil {
		t.Fatal(err)
	}
	defer rows.Close()
	out := map[string]int64{}
	for rows.Next() {
		var v string
		var at int64
		if err := rows.Scan(&v, &at); err != nil {
			t.Fatal(err)
		}
		out[v] = at
	}
	return out
}

// Every embedded file is recorded once applied, and a second boot applies nothing.
func TestMigrateRecordsEveryVersionOnce(t *testing.T) {
	s := requireStore(t)
	names, err := migrationNames()
	if err != nil {
		t.Fatal(err)
	}
	before := recordedMigrations(t, s)
	for _, n := range names {
		if _, ok := before[n]; !ok {
			t.Fatalf("%s not recorded; ledger = %v", n, before)
		}
	}
	if err := Migrate(context.Background(), s.MigratePool()); err != nil {
		t.Fatal(err)
	}
	after := recordedMigrations(t, s)
	for n, at := range before {
		if after[n] != at {
			t.Fatalf("%s re-applied on a second boot (%d -> %d)", n, at, after[n])
		}
	}
}

// Instances booting together queue on the lock instead of racing the DDL.
func TestMigrateConcurrentBootsSerialise(t *testing.T) {
	s := requireStore(t)
	if _, err := s.pool.Exec(context.Background(), `DELETE FROM schema_migrations WHERE version >= '0003'`); err != nil {
		t.Fatal(err)
	}
	var wg sync.WaitGroup
	errs := make(chan error, 4)
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			errs <- Migrate(context.Background(), s.MigratePool())
		}()
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		if err != nil {
			t.Fatalf("concurrent migrate: %v", err)
		}
	}
	names, _ := migrationNames()
	if got := recordedMigrations(t, s); len(got) != len(names) {
		t.Fatalf("ledger %v, want %s", got, strings.Join(names, ","))
	}
}
