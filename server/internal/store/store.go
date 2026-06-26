// Package store is the Postgres persistence layer for sessions and projects. It
// uses the pgx driver (the one third-party dependency sanctioned for the DB) and
// maps rows to and from protocol DTOs. Image bytes are NOT stored here — only
// the filestore-relative paths to them.
package store

import (
	"context"
	"encoding/json"
	"errors"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

// Sentinel errors surfaced to the API layer.
var (
	ErrNotFound = errors.New("store: not found")
	ErrConflict = errors.New("store: version conflict")
)

// nowMs is overridable in tests.
var nowMs = func() int64 { return time.Now().UnixMilli() }

// Store wraps a pgx connection pool.
type Store struct {
	pool *pgxpool.Pool
}

// New opens a pool against databaseURL and verifies connectivity.
func New(ctx context.Context, databaseURL string) (*Store, error) {
	pool, err := pgxpool.New(ctx, databaseURL)
	if err != nil {
		return nil, err
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	return &Store{pool: pool}, nil
}

// Close releases the pool.
func (s *Store) Close() { s.pool.Close() }

// projectCols is the canonical column list / order for project row scans.
const projectCols = `id, name, created_at, updated_at, has_image, image_w, image_h,
	source, resource, original_path, result_path, original_content, layout, owner_session, version`

// rowScanner is satisfied by both pgx.Row and pgx.Rows.
type rowScanner interface {
	Scan(dest ...any) error
}

func scanProject(row rowScanner) (protocol.ProjectRecord, error) {
	var (
		rec    protocol.ProjectRecord
		layout []byte
		owner  *string
	)
	err := row.Scan(
		&rec.ID, &rec.Name, &rec.CreatedAt, &rec.UpdatedAt, &rec.HasImage,
		&rec.ImageW, &rec.ImageH, &rec.Source, &rec.Resource,
		&rec.OriginalPath, &rec.ResultPath, &rec.OriginalContent,
		&layout, &owner, &rec.Version,
	)
	if err != nil {
		return protocol.ProjectRecord{}, err
	}
	rec.Layout = layout
	if owner != nil {
		rec.OwnerSession = *owner
	}
	return rec, nil
}

// ----- Sessions -----

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

// DeleteSession revokes a session by id.
func (s *Store) DeleteSession(ctx context.Context, id string) error {
	_, err := s.pool.Exec(ctx, `DELETE FROM sessions WHERE id = $1`, id)
	return err
}

// ----- Projects -----

// ListProjects returns project metadata (without layout/original content),
// newest-updated first.
func (s *Store) ListProjects(ctx context.Context) ([]protocol.ProjectRecord, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT `+projectCols+` FROM projects ORDER BY updated_at DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []protocol.ProjectRecord{}
	for rows.Next() {
		rec, err := scanProject(rows)
		if err != nil {
			return nil, err
		}
		rec.Layout = nil         // keep the list light
		rec.OriginalContent = "" // payload fetched on demand via GetProject
		out = append(out, rec)
	}
	return out, rows.Err()
}

// GetProject returns the full project including layout and original content.
func (s *Store) GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error) {
	rec, err := scanProject(s.pool.QueryRow(ctx,
		`SELECT `+projectCols+` FROM projects WHERE id = $1`, id))
	if errors.Is(err, pgx.ErrNoRows) {
		return protocol.ProjectRecord{}, ErrNotFound
	}
	return rec, err
}

// CreateProject inserts a new project owned by ownerSession.
func (s *Store) CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	now := nowMs()
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
	rec, err := scanProject(s.pool.QueryRow(ctx,
		`INSERT INTO projects
			(id, name, created_at, updated_at, has_image, image_w, image_h,
			 source, resource, original_content, layout, owner_session, version)
		 VALUES ($1,$2,$3,$3,$4,$5,$6,$7,$8,$9,$10::jsonb,$11,0)
		 RETURNING `+projectCols,
		id, name, now, req.HasImage, req.ImageW, req.ImageH,
		req.Source, req.Resource, req.OriginalContent, layout, owner))
	return rec, err
}

// UpdateProject applies a last-writer-wins update guarded by expectedVersion.
// A stale version yields ErrConflict; a missing project yields ErrNotFound.
func (s *Store) UpdateProject(ctx context.Context, id string, name *string, layout json.RawMessage, expectedVersion int64) (protocol.ProjectRecord, error) {
	var layoutArg any
	if len(layout) > 0 {
		layoutArg = string(layout)
	}
	rec, err := scanProject(s.pool.QueryRow(ctx,
		`UPDATE projects SET
			name = COALESCE($2, name),
			layout = COALESCE($3::jsonb, layout),
			updated_at = $4,
			version = version + 1
		 WHERE id = $1 AND version = $5
		 RETURNING `+projectCols,
		id, name, layoutArg, nowMs(), expectedVersion))
	if errors.Is(err, pgx.ErrNoRows) {
		// Disambiguate not-found from version conflict.
		if _, e := s.GetProject(ctx, id); errors.Is(e, ErrNotFound) {
			return protocol.ProjectRecord{}, ErrNotFound
		}
		return protocol.ProjectRecord{}, ErrConflict
	}
	return rec, err
}

// SetFile records a stored file path for a project and bumps version/updated_at.
// For the original image it also sets has_image and the image dimensions.
func (s *Store) SetFile(ctx context.Context, id, kind, relPath string, w, h int) (protocol.ProjectRecord, error) {
	now := nowMs()
	var (
		rec protocol.ProjectRecord
		err error
	)
	switch kind {
	case protocol.KindOriginal:
		rec, err = scanProject(s.pool.QueryRow(ctx,
			`UPDATE projects SET original_path=$2, has_image=true, image_w=$3, image_h=$4,
				updated_at=$5, version=version+1
			 WHERE id=$1 RETURNING `+projectCols,
			id, relPath, w, h, now))
	case protocol.KindResult:
		rec, err = scanProject(s.pool.QueryRow(ctx,
			`UPDATE projects SET result_path=$2, updated_at=$3, version=version+1
			 WHERE id=$1 RETURNING `+projectCols,
			id, relPath, now))
	default:
		return protocol.ProjectRecord{}, errors.New("store: invalid file kind")
	}
	if errors.Is(err, pgx.ErrNoRows) {
		return protocol.ProjectRecord{}, ErrNotFound
	}
	return rec, err
}

// DeleteProject removes a project row. Missing rows are not an error.
func (s *Store) DeleteProject(ctx context.Context, id string) error {
	_, err := s.pool.Exec(ctx, `DELETE FROM projects WHERE id = $1`, id)
	return err
}
