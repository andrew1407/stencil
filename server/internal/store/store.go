// Package store is the Postgres persistence layer for sessions and projects. It
// uses the pgx driver (the one third-party dependency sanctioned for the DB) and
// maps rows to and from protocol DTOs. Image bytes are NOT stored here — only
// the filestore-relative paths to them.
package store

import (
	"errors"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

// Sentinel errors surfaced to the API layer.
var (
	ErrNotFound = errors.New("store: not found")
	ErrConflict = errors.New("store: version conflict")
)

// DefaultOpTimeout is the deadline a caller puts around one Store call when it is configured with none:
// long enough for a cold index scan, short enough that a stuck query releases its pool connection.
const DefaultOpTimeout = 10 * time.Second

// Store wraps a pgx connection pool.
type Store struct {
	pool *pgxpool.Pool
}

// Close releases the pool.
func (s *Store) Close() { s.pool.Close() }
