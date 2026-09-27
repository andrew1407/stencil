package store

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	"stencil/server/internal/protocol"
)

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

// ProjectPage bounds one ListProjects call; a zero Limit lists every project. Limit is validated at the
// request boundary (validate.ListLimit), so the store takes it as given.
type ProjectPage struct {
	Limit int
	After ProjectCursor
}

// ListProjects returns project metadata (never the layout), newest
// first. The id tiebreak makes the order total, so a page cannot skip or repeat.
func (s *Store) ListProjects(ctx context.Context, page ProjectPage) ([]protocol.ProjectRecord, error) {
	q := `SELECT ` + projectMetaCols + ` FROM projects`
	args := make([]any, 0)
	if page.After.ID != "" {
		q += ` WHERE (updated_at, id) < ($1, $2)`
		args = append(args, page.After.UpdatedAt, page.After.ID)
	}
	q += ` ORDER BY updated_at DESC, id DESC`
	if page.Limit > 0 {
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
		rec, err := scanProject(rows, metaOnly)
		if err != nil {
			return nil, err
		}
		out = append(out, rec)
	}
	return out, rows.Err()
}
