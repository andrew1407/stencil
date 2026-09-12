package protocol

import "encoding/json"

// ProjectRecord is the canonical project metadata exchanged over REST and WS.
type ProjectRecord struct {
	ID          string   `json:"id"`
	Name        string   `json:"name"`
	CreatedAt   int64    `json:"createdAt"`           // epoch ms
	UpdatedAt   int64    `json:"updatedAt"`           // epoch ms (lists sort desc)
	ExpiresAt   int64    `json:"expiresAt,omitempty"` // epoch ms; 0 = never (swept once past)
	HasImage    bool     `json:"hasImage"`
	ImageW      int      `json:"imageW"`
	ImageH      int      `json:"imageH"`
	Source      string   `json:"source,omitempty"`      // media URL (provenance)
	Resource    string   `json:"resource,omitempty"`    // origin web page (provenance)
	Color       string   `json:"color,omitempty"`       // custom accent "#rrggbb" or "" (theme default)
	Keywords    []string `json:"keywords,omitempty"`    // search keywords ([] = none)
	Description string   `json:"description,omitempty"` // free-text project description ("" = none)
	// Blank-image projects: BlankColor is the solid background fill ("#rrggbb"); Blank is derived
	// (blankColor != ""). "" / false for ordinary image projects. Recolourable after creation.
	BlankColor string `json:"blankColor,omitempty"`
	Blank      bool   `json:"blank,omitempty"`

	// Server-only storage fields.
	OriginalPath    string          `json:"originalPath,omitempty"`    // filestore-relative
	ResultPath      string          `json:"resultPath,omitempty"`      // filestore-relative
	OriginalContent string          `json:"originalContent,omitempty"` // original payload kept for re-fetch
	Layout          json.RawMessage `json:"layout,omitempty"`          // JSON layout payload
	Version         int64           `json:"version"`                   // monotonic edit version (LWW guard)
	OwnerSession    string          `json:"ownerSession,omitempty"`
}

// ----- REST DTOs -----

// TokenResponse is returned by POST /auth/token.
type TokenResponse struct {
	Token     string `json:"token"`
	ExpiresAt int64  `json:"expiresAt"` // epoch ms
}

// ProjectListResponse is returned by GET /projects. NextCursor appears only when
// the request asked for a page (?limit=) and more rows may follow.
type ProjectListResponse struct {
	Projects   []ProjectRecord `json:"projects"`
	NextCursor string          `json:"nextCursor,omitempty"` // pass back as ?after=
}

// ProjectResponse wraps a single project plus its payload (GET /projects/{id}).
type ProjectResponse struct {
	Project         ProjectRecord   `json:"project"`
	Layout          json.RawMessage `json:"layout,omitempty"`
	OriginalContent string          `json:"originalContent,omitempty"`
}

// CreateProjectRequest is the body of POST /projects.
type CreateProjectRequest struct {
	Name            string          `json:"name,omitempty"`
	Source          string          `json:"source,omitempty"`
	Resource        string          `json:"resource,omitempty"`
	Color           string          `json:"color,omitempty"`
	Description     string          `json:"description,omitempty"`
	Keywords        []string        `json:"keywords,omitempty"`
	BlankColor      string          `json:"blankColor,omitempty"`
	HasImage        bool            `json:"hasImage,omitempty"`
	ImageW          int             `json:"imageW,omitempty"`
	ImageH          int             `json:"imageH,omitempty"`
	OriginalContent string          `json:"originalContent,omitempty"`
	Layout          json.RawMessage `json:"layout,omitempty"`
	// Explicit create-time expiry; without one a project expires only if PROJECT_TTL is set.
	ExpiresAt int64 `json:"expiresAt,omitempty"` // epoch ms; 0/absent = never
}

// UpdateProjectRequest is the body of PUT /projects/{id}. Version guards the
// last-writer-wins update: a stale version is rejected with ErrConflict.
type UpdateProjectRequest struct {
	Name        *string         `json:"name,omitempty"`
	Color       *string         `json:"color,omitempty"`       // nil => unchanged (like Name)
	Description *string         `json:"description,omitempty"` // nil => unchanged; "" clears
	Keywords    *[]string       `json:"keywords,omitempty"`    // nil => unchanged; [] clears
	BlankColor  *string         `json:"blankColor,omitempty"`  // nil => unchanged; "" clears (→ not blank)
	ExpiresAt   *int64          `json:"expiresAt,omitempty"`   // nil => unchanged; 0 = keep forever
	Layout      json.RawMessage `json:"layout,omitempty"`
	Version     int64           `json:"version"`
}

// FileWriteResponse is returned by POST /projects/{id}/files/{kind}.
type FileWriteResponse struct {
	Path string `json:"path"`
	W    int    `json:"w"`
	H    int    `json:"h"`
}

// ErrorResponse is the JSON body for any non-2xx REST response.
type ErrorResponse struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}
