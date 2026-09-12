package store

import (
	"strings"

	"stencil/server/internal/protocol"
)

// projectCols is the column list / order for full project row scans.
const projectCols = `id, name, created_at, updated_at, expires_at, has_image, image_w, image_h,
	source, resource, color, description, original_path, result_path, original_content, layout, owner_session, version, keywords_arr, blank_color`

// scanProject's payload switch: only full rows carry original_content + layout.
const (
	withPayload    = true
	withoutPayload = false
)

// normalizeKeywords is the keyword rule, applied once at the write boundary: trim,
// drop blanks, dedupe case-insensitively, keep first-seen order. Never nil — a nil
// slice would encode as SQL NULL, which the UPDATE reads as "unchanged".
func normalizeKeywords(kw []string) []string {
	out := make([]string, 0)
	seen := map[string]bool{}
	for _, k := range kw {
		k = strings.TrimSpace(k)
		if k == "" {
			continue
		}
		lk := strings.ToLower(k)
		if seen[lk] {
			continue
		}
		seen[lk] = true
		out = append(out, k)
	}
	return out
}

// joinKeywords feeds the legacy `keywords` column: written but never read for one
// release, so a rollback keeps its data. Migration 0003 drops both.
func joinKeywords(kw []string) string { return strings.Join(normalizeKeywords(kw), "\n") }

// rowScanner is satisfied by both pgx.Row and pgx.Rows.
type rowScanner interface {
	Scan(dest ...any) error
}

func scanProject(row rowScanner, payload bool) (protocol.ProjectRecord, error) {
	var (
		rec    protocol.ProjectRecord
		layout []byte
		owner  *string
	)
	dest := []any{
		&rec.ID, &rec.Name, &rec.CreatedAt, &rec.UpdatedAt, &rec.ExpiresAt, &rec.HasImage,
		&rec.ImageW, &rec.ImageH, &rec.Source, &rec.Resource, &rec.Color, &rec.Description,
		&rec.OriginalPath, &rec.ResultPath,
	}
	if payload {
		dest = append(dest, &rec.OriginalContent, &layout)
	}
	dest = append(dest, &owner, &rec.Version, &rec.Keywords, &rec.BlankColor)
	if err := row.Scan(dest...); err != nil {
		return protocol.ProjectRecord{}, err
	}
	rec.Layout = layout
	rec.Blank = rec.BlankColor != "" // derived: a non-empty fill means a blank-image project
	if owner != nil {
		rec.OwnerSession = *owner
	}
	return rec, nil
}

// scanProjectRow scans one full projectCols row.
func scanProjectRow(row rowScanner) (protocol.ProjectRecord, error) {
	return scanProject(row, withPayload)
}
