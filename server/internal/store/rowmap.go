package store

import (
	"strings"

	"stencil/server/internal/protocol"
)

// projectMetaCols is every column but the layout payload. Lists, write RETURNINGs, existence checks and the
// file routes read only these; the original's bytes are never a column, only the filestore's.
const projectMetaCols = `id, name, created_at, updated_at, expires_at, has_image, image_w, image_h,
	source, resource, color, description, original_path, result_path, owner_session, version, keywords_arr, blank_color,
	original_hash`

// projectCols is the whole row, the layout after the metadata so one scanner reads both column sets:
// GET /projects/{id}.
const projectCols = projectMetaCols + `, layout`

// rowPayload picks whether a scan expects the layout after the metadata.
type rowPayload int

const (
	metaOnly   rowPayload = iota // projectMetaCols
	withLayout                   // projectCols
)

// normalizeKeywords is the keyword rule at the write boundary: trim, drop blanks, dedupe
// case-insensitively, keep first-seen order. Never nil — a nil slice is SQL NULL, read as "unchanged".
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

// rowScanner is satisfied by both pgx.Row and pgx.Rows.
type rowScanner interface {
	Scan(dest ...any) error
}

func scanProject(row rowScanner, payload rowPayload) (protocol.ProjectRecord, error) {
	var (
		rec    protocol.ProjectRecord
		layout []byte
		owner  *string
		hash   *string // NULL until an original is stored with its hash
	)
	dest := []any{
		&rec.ID, &rec.Name, &rec.CreatedAt, &rec.UpdatedAt, &rec.ExpiresAt, &rec.HasImage,
		&rec.ImageW, &rec.ImageH, &rec.Source, &rec.Resource, &rec.Color, &rec.Description,
		&rec.OriginalPath, &rec.ResultPath, &owner, &rec.Version, &rec.Keywords, &rec.BlankColor, &hash,
	}
	if payload == withLayout {
		dest = append(dest, &layout)
	}
	if err := row.Scan(dest...); err != nil {
		return protocol.ProjectRecord{}, err
	}
	rec.Layout = layout
	rec.Blank = rec.BlankColor != "" // derived: a non-empty fill means a blank-image project
	if owner != nil {
		rec.OwnerSession = *owner
	}
	if hash != nil {
		rec.OriginalHash = *hash
	}
	return rec, nil
}
