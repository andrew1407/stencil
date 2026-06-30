// Package protocol declares the wire DTOs and the WebSocket message envelope
// shared by the server and every Stencil front-end. It is the single source of
// truth the browser/desktop/CLI/extension clients mirror (they re-declare these
// shapes, they do not import them).
//
// ProjectRecord mirrors core/state/projectsStore.hpp ProjectMeta semantics
// (epoch-millisecond timestamps, source = media URL, resource = origin page) and
// adds the server-only storage fields. Per the Stencil parity contract, server/
// is a protocol adapter: it re-declares this shape in Go rather than reaching
// into core/.
package protocol

import "encoding/json"

// ProjectRecord is the canonical project metadata exchanged over REST and WS.
type ProjectRecord struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	CreatedAt int64  `json:"createdAt"` // epoch ms
	UpdatedAt int64  `json:"updatedAt"` // epoch ms (lists sort desc)
	HasImage  bool   `json:"hasImage"`
	ImageW    int    `json:"imageW"`
	ImageH    int    `json:"imageH"`
	Source    string `json:"source,omitempty"`   // media URL (provenance)
	Resource  string `json:"resource,omitempty"` // origin web page (provenance)
	Color     string `json:"color,omitempty"`    // custom accent "#rrggbb" or "" (theme default)

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

// ProjectListResponse is returned by GET /projects.
type ProjectListResponse struct {
	Projects []ProjectRecord `json:"projects"`
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
	HasImage        bool            `json:"hasImage,omitempty"`
	ImageW          int             `json:"imageW,omitempty"`
	ImageH          int             `json:"imageH,omitempty"`
	OriginalContent string          `json:"originalContent,omitempty"`
	Layout          json.RawMessage `json:"layout,omitempty"`
}

// UpdateProjectRequest is the body of PUT /projects/{id}. Version guards the
// last-writer-wins update: a stale version is rejected with ErrConflict.
type UpdateProjectRequest struct {
	Name    *string         `json:"name,omitempty"`
	Color   *string         `json:"color,omitempty"` // nil => unchanged (like Name)
	Layout  json.RawMessage `json:"layout,omitempty"`
	Version int64           `json:"version"`
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

// File kinds accepted by the files endpoints and the filestore.
const (
	KindOriginal = "original"
	KindResult   = "result"
)

// ----- WebSocket envelope -----

// WS message type identifiers.
const (
	// client -> server
	WSHello     = "hello"     // MUST be the first frame: { token, clientId, name? }
	WSSubscribe = "subscribe" // { sinceVersion? } request a snapshot
	WSEdit      = "edit"      // { version, op, payload } layout mutation
	WSCursor    = "cursor"    // { x, y } ephemeral, relayed not persisted
	WSPresence  = "presence"  // { state } ephemeral
	WSSave      = "save"      // commit layout -> store
	WSPing      = "ping"

	// server -> client
	WSWelcome   = "welcome"   // { project, layout, version, peers[] }
	WSPeerJoin  = "peer-join" // { clientId, name }
	WSPeerLeave = "peer-leave"
	WSSynced    = "synced" // { version, resultPath? } ack/commit confirmation
	WSError     = "error"  // { code, message }
	WSPong      = "pong"
	WSProjectEv = "project-event" // global /events feed: { event, project }
)

// Project-event sub-types for the global /events feed.
const (
	EventCreated = "created"
	EventUpdated = "updated"
	EventDeleted = "deleted"
)

// Error codes carried in WSMessage.Code and ErrorResponse.Code.
const (
	CodeUnauthorized = "unauthorized"
	CodeBadVersion   = "badVersion"
	CodeNotFound     = "notFound"
	CodeConflict     = "conflict"
	CodeBadRequest   = "badRequest"
	CodeInternal     = "internal"
)

// Peer identifies a participant in a live edit session.
type Peer struct {
	ClientID string `json:"clientId"`
	Name     string `json:"name,omitempty"`
}

// WSMessage is the JSON envelope for every text frame in both directions. Fields
// are optional per message type; Type selects which are meaningful.
type WSMessage struct {
	Type string `json:"type"`

	// auth / identity (hello). ProjectID routes a hello frame to a project
	// session; an empty ProjectID selects the global /events feed. It carries
	// the route over transports (TCP) that have no request path.
	Token     string `json:"token,omitempty"`
	ClientID  string `json:"clientId,omitempty"`
	Name      string `json:"name,omitempty"`
	ProjectID string `json:"projectId,omitempty"`

	// edit / sync
	Version      int64           `json:"version,omitempty"`
	SinceVersion int64           `json:"sinceVersion,omitempty"`
	Op           string          `json:"op,omitempty"`
	Payload      json.RawMessage `json:"payload,omitempty"`

	// snapshots / events
	Project    *ProjectRecord  `json:"project,omitempty"`
	Layout     json.RawMessage `json:"layout,omitempty"`
	Peers      []Peer          `json:"peers,omitempty"`
	Event      string          `json:"event,omitempty"`
	ResultPath string          `json:"resultPath,omitempty"`

	// relay bookkeeping
	FromClientID string `json:"fromClientId,omitempty"`

	// presence / cursor
	X     float64 `json:"x,omitempty"`
	Y     float64 `json:"y,omitempty"`
	State string  `json:"state,omitempty"`

	// errors
	Code    string `json:"code,omitempty"`
	Message string `json:"message,omitempty"`
}
