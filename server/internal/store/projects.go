package store

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"

	"github.com/jackc/pgx/v5"

	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
)

// defaultSweepLimit caps one DeleteExpiredProjects batch.
const defaultSweepLimit = 500

// GetProject returns the full project including layout and original content.
func (s *Store) GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error) {
	rec, err := scanProjectRow(s.pool.QueryRow(ctx,
		`SELECT `+projectCols+` FROM projects WHERE id = $1`, id))
	if errors.Is(err, pgx.ErrNoRows) {
		return protocol.ProjectRecord{}, ErrNotFound
	}
	if err != nil {
		return protocol.ProjectRecord{}, fmt.Errorf("get project %s: %w", id, err)
	}
	return rec, nil
}

// CreateProject inserts a new project owned by ownerSession.
func (s *Store) CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	now := clock.NowMs()
	id, err := newProjectID(now)
	if err != nil {
		return protocol.ProjectRecord{}, err
	}
	name := req.Name
	if name == "" {
		name = "Untitled"
	}
	var owner *string
	if ownerSession != "" {
		owner = &ownerSession
	}
	var layout any
	if len(req.Layout) > 0 {
		layout = string(req.Layout)
	}
	rec, err := scanProjectRow(s.pool.QueryRow(ctx,
		`INSERT INTO projects
			(id, name, created_at, updated_at, expires_at, has_image, image_w, image_h,
			 source, resource, color, description, original_content, layout, owner_session, keywords, keywords_arr, blank_color, version)
		 VALUES ($1,$2,$3,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13::jsonb,$14,$15,$16,$17,0)
		 RETURNING `+projectCols,
		id, name, now, req.ExpiresAt, req.HasImage, req.ImageW, req.ImageH,
		req.Source, req.Resource, req.Color, req.Description, req.OriginalContent, layout, owner,
		joinKeywords(req.Keywords), normalizeKeywords(req.Keywords), req.BlankColor))
	if err != nil {
		return protocol.ProjectRecord{}, fmt.Errorf("create project: %w", err)
	}
	return rec, nil
}

// ProjectPatch bundles the optionally-updated fields of an UpdateProject call.
// A nil pointer (or empty Layout) leaves that column untouched.
type ProjectPatch struct {
	Name        *string
	Color       *string
	Description *string   // nil => unchanged; "" clears
	Keywords    *[]string // nil => unchanged; empty slice clears
	BlankColor  *string   // nil => unchanged; "" clears (→ not blank)
	ExpiresAt   *int64
	Layout      json.RawMessage
}

// UpdateProject applies a last-writer-wins update guarded by expectedVersion.
// A stale version yields ErrConflict; a missing project yields ErrNotFound.
func (s *Store) UpdateProject(ctx context.Context, id string, patch ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error) {
	var layoutArg any
	if len(patch.Layout) > 0 {
		layoutArg = string(patch.Layout)
	}
	// nil => leave keywords untouched (COALESCE); a (possibly empty) slice => set/clear.
	var kwArg, kwText any
	if patch.Keywords != nil {
		kwArg, kwText = normalizeKeywords(*patch.Keywords), joinKeywords(*patch.Keywords)
	}
	rec, err := scanProjectRow(s.pool.QueryRow(ctx,
		`UPDATE projects SET
			name = COALESCE($2, name),
			color = COALESCE($3, color),
			description = COALESCE($10, description),
			keywords_arr = COALESCE($8::text[], keywords_arr),
			keywords = COALESCE($11, keywords),
			blank_color = COALESCE($9, blank_color),
			expires_at = COALESCE($7, expires_at),
			layout = COALESCE($4::jsonb, layout),
			updated_at = $5,
			version = version + 1
		 WHERE id = $1 AND version = $6
		 RETURNING `+projectCols,
		id, patch.Name, patch.Color, layoutArg, clock.NowMs(), expectedVersion, patch.ExpiresAt, kwArg,
		patch.BlankColor, patch.Description, kwText))
	if errors.Is(err, pgx.ErrNoRows) {
		// Disambiguate not-found from version conflict.
		if _, e := s.GetProject(ctx, id); errors.Is(e, ErrNotFound) {
			return protocol.ProjectRecord{}, ErrNotFound
		}
		return protocol.ProjectRecord{}, ErrConflict
	}
	if err != nil {
		return protocol.ProjectRecord{}, fmt.Errorf("update project %s: %w", id, err)
	}
	return rec, nil
}

// SetFile records a stored file path for a project and bumps version/updated_at.
// For the original image it also sets has_image and the image dimensions.
func (s *Store) SetFile(ctx context.Context, id, kind, relPath string, w, h int) (protocol.ProjectRecord, error) {
	now := clock.NowMs()
	var (
		rec protocol.ProjectRecord
		err error
	)
	switch kind {
	case protocol.KindOriginal:
		rec, err = scanProjectRow(s.pool.QueryRow(ctx,
			`UPDATE projects SET original_path=$2, has_image=true, image_w=$3, image_h=$4,
				updated_at=$5, version=version+1
			 WHERE id=$1 RETURNING `+projectCols,
			id, relPath, w, h, now))
	case protocol.KindResult:
		rec, err = scanProjectRow(s.pool.QueryRow(ctx,
			`UPDATE projects SET result_path=$2, updated_at=$3, version=version+1
			 WHERE id=$1 RETURNING `+projectCols,
			id, relPath, now))
	default:
		return protocol.ProjectRecord{}, errors.New("store: invalid file kind")
	}
	if errors.Is(err, pgx.ErrNoRows) {
		return protocol.ProjectRecord{}, ErrNotFound
	}
	if err != nil {
		return protocol.ProjectRecord{}, fmt.Errorf("set %s file for project %s: %w", kind, id, err)
	}
	return rec, nil
}

// DeleteProject removes a project row. Missing rows are not an error.
func (s *Store) DeleteProject(ctx context.Context, id string) error {
	_, err := s.pool.Exec(ctx, `DELETE FROM projects WHERE id = $1`, id)
	return err
}

// DeleteExpiredProjects removes up to limit projects whose expiry has passed (expires_at in (0, now]) and
// returns their ids; a zero expires_at is never swept. The caller loops until a pass comes back short.
func (s *Store) DeleteExpiredProjects(ctx context.Context, now int64, limit int) ([]string, error) {
	if limit <= 0 || limit > defaultSweepLimit {
		limit = defaultSweepLimit
	}
	rows, err := s.pool.Query(ctx,
		`DELETE FROM projects WHERE id IN (
			SELECT id FROM projects WHERE expires_at > 0 AND expires_at <= $1
			ORDER BY expires_at LIMIT $2
		 ) RETURNING id`, now, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	ids := make([]string, 0)
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			return nil, err
		}
		ids = append(ids, id)
	}
	return ids, rows.Err()
}
