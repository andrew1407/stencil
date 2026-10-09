package store

import (
	"context"
	"errors"

	"github.com/jackc/pgx/v5"

	"stencil/server/internal/auth"
)

// CreateSession persists a new session for an already-hashed token.
func (s *Store) CreateSession(ctx context.Context, tokenHash []byte, label string, createdAt, expiresAt int64) (auth.Session, error) {
	id, err := newSessionID(createdAt)
	if err != nil {
		return auth.Session{}, err
	}
	_, err = s.pool.Exec(ctx,
		`INSERT INTO sessions (id, token_hash, label, created_at, expires_at) VALUES ($1,$2,$3,$4,$5)`,
		id, tokenHash, label, createdAt, expiresAt)
	if err != nil {
		return auth.Session{}, err
	}
	return auth.Session{ID: id, Label: label, CreatedAt: createdAt, ExpiresAt: expiresAt}, nil
}

// ResolveToken implements auth.SessionResolver.
func (s *Store) ResolveToken(ctx context.Context, tokenHash []byte) (auth.Session, error) {
	var sess auth.Session
	err := s.pool.QueryRow(ctx,
		`SELECT id, label, created_at, expires_at FROM sessions WHERE token_hash = $1`, tokenHash).
		Scan(&sess.ID, &sess.Label, &sess.CreatedAt, &sess.ExpiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return auth.Session{}, auth.ErrInvalidToken
	}
	if err != nil {
		return auth.Session{}, err
	}
	return sess, nil
}

// DeleteExpiredSessions removes up to limit sessions whose expiry has passed (expires_at in (0, now]) and
// reports how many went; projects they own keep their row (owner_session is ON DELETE SET NULL).
func (s *Store) DeleteExpiredSessions(ctx context.Context, now int64, limit int) (int, error) {
	if limit <= 0 || limit > MaxSweepBatch {
		limit = MaxSweepBatch
	}
	tag, err := s.pool.Exec(ctx,
		`DELETE FROM sessions WHERE id IN (
			SELECT id FROM sessions WHERE expires_at > 0 AND expires_at <= $1
			ORDER BY expires_at LIMIT $2)`, now, limit)
	if err != nil {
		return 0, err
	}
	return int(tag.RowsAffected()), nil
}
