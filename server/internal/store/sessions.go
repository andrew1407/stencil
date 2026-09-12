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
