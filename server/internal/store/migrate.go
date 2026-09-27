package store

import (
	"context"
	"embed"
	"fmt"
	"sort"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"stencil/server/internal/clock"
)

//go:embed migrations/*.sql
var migrationsFS embed.FS

// migrationLock is the pg_advisory_xact_lock key ("stencil" in ASCII) that serialises instances
// booting against one database; being transaction-scoped, it cannot leak onto a pooled connection.
const migrationLock int64 = 0x7374656e63696c

// Migrate is MigrateWith for a database that holds no legacy inline original; one that does is refused.
func Migrate(ctx context.Context, pool *pgxpool.Pool) error {
	return MigrateWith(ctx, pool, nil)
}

// MigrateWith applies every unrecorded migration in lexical order, in one transaction under the advisory
// lock (each idempotent: a pre-versioning database re-runs them); move stores 0006's inline originals.
func MigrateWith(ctx context.Context, pool *pgxpool.Pool, move MoveOriginal) error {
	names, err := migrationNames()
	if err != nil {
		return err
	}
	return pgx.BeginFunc(ctx, pool, func(tx pgx.Tx) error {
		if _, err := tx.Exec(ctx, `SELECT pg_advisory_xact_lock($1)`, migrationLock); err != nil {
			return fmt.Errorf("migration lock: %w", err)
		}
		if _, err := tx.Exec(ctx, `CREATE TABLE IF NOT EXISTS schema_migrations (
			version    text   PRIMARY KEY,
			applied_at bigint NOT NULL)`); err != nil {
			return fmt.Errorf("schema_migrations: %w", err)
		}
		applied, err := appliedMigrations(ctx, tx)
		if err != nil {
			return err
		}
		for _, name := range names {
			if applied[name] {
				continue
			}
			if err := applyMigration(ctx, tx, name, move); err != nil {
				return err
			}
		}
		return nil
	})
}

func migrationNames() ([]string, error) {
	entries, err := migrationsFS.ReadDir("migrations")
	if err != nil {
		return nil, err
	}
	names := make([]string, 0, len(entries))
	for _, e := range entries {
		if !e.IsDir() {
			names = append(names, e.Name())
		}
	}
	for name := range goSteps {
		names = append(names, name)
	}
	sort.Strings(names)
	return names, nil
}

func appliedMigrations(ctx context.Context, tx pgx.Tx) (map[string]bool, error) {
	rows, err := tx.Query(ctx, `SELECT version FROM schema_migrations`)
	if err != nil {
		return nil, fmt.Errorf("read schema_migrations: %w", err)
	}
	versions, err := pgx.CollectRows(rows, pgx.RowTo[string])
	if err != nil {
		return nil, fmt.Errorf("read schema_migrations: %w", err)
	}
	applied := make(map[string]bool, len(versions))
	for _, v := range versions {
		applied[v] = true
	}
	return applied, nil
}

func applyMigration(ctx context.Context, tx pgx.Tx, name string, move MoveOriginal) error {
	if step, ok := goSteps[name]; ok {
		if err := step(ctx, tx, move); err != nil {
			return fmt.Errorf("migration %s: %w", name, err)
		}
	} else if err := applySQL(ctx, tx, name); err != nil {
		return err
	}
	if _, err := tx.Exec(ctx, `INSERT INTO schema_migrations (version, applied_at) VALUES ($1, $2)`,
		name, clock.NowMs()); err != nil {
		return fmt.Errorf("record migration %s: %w", name, err)
	}
	return nil
}

func applySQL(ctx context.Context, tx pgx.Tx, name string) error {
	sqlBytes, err := migrationsFS.ReadFile("migrations/" + name)
	if err != nil {
		return err
	}
	if _, err := tx.Exec(ctx, string(sqlBytes)); err != nil {
		return fmt.Errorf("migration %s: %w", name, err)
	}
	return nil
}

// MigratePool exposes the pool for callers that opened the Store and want to run
// migrations against it.
func (s *Store) MigratePool() *pgxpool.Pool { return s.pool }
