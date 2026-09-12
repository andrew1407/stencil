package store

import (
	"context"
	"strconv"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

// PoolOptions tunes the pgx pool. Zero fields keep pgx's own defaults.
type PoolOptions struct {
	MaxConns         int
	MinConns         int
	StatementTimeout time.Duration // server-side per-statement cap; 0 = none
}

// minPoolConns floors the pool: pgx's max(4, NumCPU) default leaves a 2-vCPU box
// four connections for the whole REST surface plus every hub save.
const minPoolConns = 16

// New opens a pool with default sizing and verifies connectivity.
func New(ctx context.Context, databaseURL string) (*Store, error) {
	return NewWithPool(ctx, databaseURL, PoolOptions{})
}

// NewWithPool is New with explicit pool tunables.
func NewWithPool(ctx context.Context, databaseURL string, opts PoolOptions) (*Store, error) {
	cfg, err := pgxpool.ParseConfig(databaseURL)
	if err != nil {
		return nil, err
	}
	switch {
	case opts.MaxConns > 0:
		cfg.MaxConns = int32(opts.MaxConns)
	case cfg.MaxConns < minPoolConns:
		cfg.MaxConns = minPoolConns
	}
	if opts.MinConns > 0 {
		cfg.MinConns = int32(opts.MinConns)
	}
	if cfg.MinConns > cfg.MaxConns {
		cfg.MinConns = cfg.MaxConns
	}
	if opts.StatementTimeout > 0 {
		// Postgres cancels a statement past this, freeing its pool connection.
		cfg.ConnConfig.RuntimeParams["statement_timeout"] = strconv.FormatInt(opts.StatementTimeout.Milliseconds(), 10)
	}
	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, err
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	return &Store{pool: pool}, nil
}
