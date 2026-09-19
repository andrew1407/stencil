package store

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	"stencil/server/internal/protocol"
)

// projectListCols is projectCols minus original_content and layout, same order (scanProject skips exactly
// that pair). One row's original_content can hold a whole MAX_BODY_BYTES image.
const projectListCols = `id, name, created_at, updated_at, expires_at, has_image, image_w, image_h,
	source, resource, color, description, original_path, result_path, owner_session, version, keywords_arr, blank_color`

// defaultListLimit bounds one keyset page when a caller asks for an absurd one.
const defaultListLimit = 500

// ProjectCursor is a position in the (updated_at DESC, id DESC) list order.
type ProjectCursor struct {
	UpdatedAt int64
	ID        string
}

// String renders the opaque `?after=` token; the zero cursor is "".
func (c ProjectCursor) String() string {
	if c.ID == "" {
		return ""
	}
	return strconv.FormatInt(c.UpdatedAt, 10) + "." + c.ID
}

// ParseProjectCursor reads a String token back; "" is the start of the list.
func ParseProjectCursor(s string) (ProjectCursor, error) {
	if s == "" {
		return ProjectCursor{}, nil
	}
	updated, id, ok := strings.Cut(s, ".")
	n, err := strconv.ParseInt(updated, 10, 64)
	if !ok || id == "" || err != nil {
		return ProjectCursor{}, fmt.Errorf("store: invalid cursor %q", s)
	}
	return ProjectCursor{UpdatedAt: n, ID: id}, nil
}

// ProjectPage bounds one ListProjects call; a zero Limit lists every project.
type ProjectPage struct {
	Limit int
	After ProjectCursor
}

// ListProjects returns project metadata (never layout/original content), newest
// first. The id tiebreak makes the order total, so a page cannot skip or repeat.
func (s *Store) ListProjects(ctx context.Context, page ProjectPage) ([]protocol.ProjectRecord, error) {
	q := `SELECT ` + projectListCols + ` FROM projects`
	args := make([]any, 0)
	if page.After.ID != "" {
		q += ` WHERE (updated_at, id) < ($1, $2)`
		args = append(args, page.After.UpdatedAt, page.After.ID)
	}
	q += ` ORDER BY updated_at DESC, id DESC`
	if page.Limit > 0 {
		if page.Limit > defaultListLimit {
			page.Limit = defaultListLimit
		}
		q += ` LIMIT $` + strconv.Itoa(len(args)+1)
		args = append(args, page.Limit)
	}
	rows, err := s.pool.Query(ctx, q, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := make([]protocol.ProjectRecord, 0)
	for rows.Next() {
		rec, err := scanProject(rows, withoutPayload)
		if err != nil {
			return nil, err
		}
		out = append(out, rec)
	}
	return out, rows.Err()
}
