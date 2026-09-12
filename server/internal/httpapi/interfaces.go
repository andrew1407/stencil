package httpapi

import (
	"context"
	"io"
	"os"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// ProjectStore is the project persistence the API needs.
type ProjectStore interface {
	ListProjects(ctx context.Context, page store.ProjectPage) ([]protocol.ProjectRecord, error)
	GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error)
	CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error)
	UpdateProject(ctx context.Context, id string, patch store.ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error)
	SetFile(ctx context.Context, id, kind, relPath string, w, h int) (protocol.ProjectRecord, error)
	DeleteProject(ctx context.Context, id string) error
}

// SessionStore issues and resolves sessions.
type SessionStore interface {
	auth.SessionResolver
	CreateSession(ctx context.Context, tokenHash []byte, label string, createdAt, expiresAt int64) (auth.Session, error)
}

// SessionCounter reports how many clients are currently connected to a project's
// live edit session. The hub satisfies it; it is optional (nil in unit tests), in
// which case the delete guard treats every project as having no live connections.
type SessionCounter interface {
	ConnectionCount(projectID string) int
}

// FileStore is the byte storage the API needs. FindByKind resolves the stored
// path for filestore-only kinds (video/variantN/chat); OpenByRelPath hands back
// an *os.File so downloads can stream via http.ServeContent instead of
// buffering whole files in memory; RemoveKind backs the per-file DELETE route
// for filestore-only kinds. Nothing here holds a whole file in memory: reads go
// through the path pair, and PutStream writes the request body as it arrives.
type FileStore interface {
	PutStream(id, kind, ext string, r io.Reader) (string, error)
	FindByKind(id, kind string) (string, error)
	OpenByRelPath(rel string) (*os.File, error)
	RemoveKind(id, kind string) error
	Remove(id string) error
}

// LLM proxies chat turns to the configured provider (llm-contract.md §6.3).
// nil means the proxy is disabled (no ANTHROPIC_API_KEY): /llm/info reports
// enabled=false and /llm/chat answers 503 llmDisabled.
type LLM interface {
	Chat(ctx context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error)
	Model() string
}

// upstreamError is the llm package's classified-failure seam (*llm.UpstreamError):
// an error carrying a sanitized, client-safe reason for an UPSTREAM condition the
// user can act on — no credits, bad key, unknown model, timeout, unreachable host.
type upstreamError interface {
	error
	ClientMessage() string
}
