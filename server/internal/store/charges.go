package store

// The per-session storage ledger (0007, STORAGE_QUOTA_PER_SESSION_BYTES): one row per stored (project,
// kind) naming the session that wrote it and its bytes. It errs toward charging too little: a crash
// between a file's swap and its row leaves bytes charged to nobody, never a charge for missing bytes.

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
)

// Charge is one stored file's bytes, charged to the session that wrote them.
type Charge struct {
	ProjectID, Kind, SessionID string
	Bytes                      int64
}

// Admit records c as its (project, kind)'s charge and runs commit under c's session row lock, one upload
// per session at a time on every instance; fits sees the session's other files. A failure records nothing.
func (s *Store) Admit(ctx context.Context, c Charge, fits func(held int64) error, commit func() error) error {
	return pgx.BeginFunc(ctx, s.pool, func(tx pgx.Tx) error {
		tag, err := tx.Exec(ctx, `SELECT 1 FROM sessions WHERE id = $1 FOR UPDATE`, c.SessionID)
		if err != nil {
			return fmt.Errorf("lock session %s: %w", c.SessionID, err)
		}
		if tag.RowsAffected() == 0 {
			return fmt.Errorf("charge to session %s: it is gone", c.SessionID)
		}
		// A statement of its own: under READ COMMITTED it sees whatever the lock waited out.
		var held int64
		if err := tx.QueryRow(ctx, `SELECT COALESCE(sum(bytes), 0)::bigint FROM file_charges
			WHERE session_id = $1 AND NOT (project_id = $2 AND kind = $3)`,
			c.SessionID, c.ProjectID, c.Kind).Scan(&held); err != nil {
			return fmt.Errorf("sum session %s: %w", c.SessionID, err)
		}
		if err := fits(held); err != nil {
			return err
		}
		if _, err := tx.Exec(ctx, `INSERT INTO file_charges (project_id, kind, session_id, bytes)
			VALUES ($1, $2, $3, $4) ON CONFLICT (project_id, kind)
			DO UPDATE SET session_id = EXCLUDED.session_id, bytes = EXCLUDED.bytes`,
			c.ProjectID, c.Kind, c.SessionID, c.Bytes); err != nil {
			var pgErr *pgconn.PgError
			if errors.As(err, &pgErr) && pgErr.Code == "23503" { // foreign_key_violation: the project went
				return ErrNotFound
			}
			return fmt.Errorf("charge %s/%s: %w", c.ProjectID, c.Kind, err)
		}
		return commit()
	})
}

// Credit drops (project, kind)'s charge, ahead of removing the file it named.
func (s *Store) Credit(ctx context.Context, projectID, kind string) error {
	if _, err := s.pool.Exec(ctx, `DELETE FROM file_charges WHERE project_id = $1 AND kind = $2`, projectID, kind); err != nil {
		return fmt.Errorf("credit %s/%s: %w", projectID, kind, err)
	}
	return nil
}

// SessionCharged is what session holds across every file it wrote.
func (s *Store) SessionCharged(ctx context.Context, sessionID string) (int64, error) {
	var held int64
	err := s.pool.QueryRow(ctx, `SELECT COALESCE(sum(bytes), 0)::bigint FROM file_charges WHERE session_id = $1`,
		sessionID).Scan(&held)
	return held, err
}

// ClearCharges empties the ledger and reports how many rows went: a boot with the per-session cap off,
// so no charge outlives the uploads it described while the cap was not watching.
func (s *Store) ClearCharges(ctx context.Context) (int64, error) {
	tag, err := s.pool.Exec(ctx, `DELETE FROM file_charges`)
	if err != nil {
		return 0, fmt.Errorf("clear file_charges: %w", err)
	}
	return tag.RowsAffected(), nil
}
