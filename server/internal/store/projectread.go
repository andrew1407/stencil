package store

import (
	"context"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"

	"stencil/server/internal/protocol"
)

// GetProject returns the whole project, the layout included: GET /projects/{id}.
func (s *Store) GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error) {
	return s.getProject(ctx, id, projectCols, withLayout)
}

// GetProjectMeta is the metadata alone, what the file routes read (originalPath, resultPath).
func (s *Store) GetProjectMeta(ctx context.Context, id string) (protocol.ProjectRecord, error) {
	return s.getProject(ctx, id, projectMetaCols, metaOnly)
}

// ProjectExists reports whether a row with id exists, reading nothing else.
func (s *Store) ProjectExists(ctx context.Context, id string) (bool, error) {
	var exists bool
	err := s.pool.QueryRow(ctx, `SELECT EXISTS (SELECT 1 FROM projects WHERE id = $1)`, id).Scan(&exists)
	if err != nil {
		return false, fmt.Errorf("check project %s: %w", id, err)
	}
	return exists, nil
}

// OwnerProjectIDs lists every project sharing id's owner session, id included: none when id has no owner
// (never had one, or its session was swept) or no row. It feeds STORAGE_QUOTA_PER_OWNER_BYTES.
func (s *Store) OwnerProjectIDs(ctx context.Context, id string) ([]string, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT id FROM projects WHERE owner_session = (SELECT owner_session FROM projects WHERE id = $1)`, id)
	if err != nil {
		return nil, fmt.Errorf("owner projects of %s: %w", id, err)
	}
	ids, err := pgx.CollectRows(rows, pgx.RowTo[string])
	if err != nil {
		return nil, fmt.Errorf("owner projects of %s: %w", id, err)
	}
	return ids, nil
}

func (s *Store) getProject(ctx context.Context, id, cols string, payload rowPayload) (protocol.ProjectRecord, error) {
	rec, err := scanProject(s.pool.QueryRow(ctx, `SELECT `+cols+` FROM projects WHERE id = $1`, id), payload)
	if errors.Is(err, pgx.ErrNoRows) {
		return protocol.ProjectRecord{}, ErrNotFound
	}
	if err != nil {
		return protocol.ProjectRecord{}, fmt.Errorf("get project %s: %w", id, err)
	}
	return rec, nil
}
