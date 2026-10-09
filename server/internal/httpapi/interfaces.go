package httpapi

import (
	"context"
	"io"
	"os"

	"stencil/server/internal/auth"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// ProjectStore is the project persistence the API needs. Only GET /projects/{id} reads the layout
// (GetProject); every other route reads metadata or existence, and every write returns metadata.
type ProjectStore interface {
	ListProjects(ctx context.Context, page store.ProjectPage) ([]protocol.ProjectRecord, error)
	GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error)
	GetProjectMeta(ctx context.Context, id string) (protocol.ProjectRecord, error)
	ProjectExists(ctx context.Context, id string) (bool, error)
	OwnerProjectIDs(ctx context.Context, id string) ([]string, error)
	CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error)
	UpdateProject(ctx context.Context, id string, patch store.ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error)
	SetFile(ctx context.Context, id string, f store.StoredFile) (protocol.ProjectRecord, error)
	DeleteProject(ctx context.Context, id string) error
}

// SessionStore issues and resolves sessions.
type SessionStore interface {
	auth.SessionResolver
	CreateSession(ctx context.Context, tokenHash []byte, label string, createdAt, expiresAt int64) (auth.Session, error)
}

// FileStore is the byte storage the API needs. Nothing here holds a whole file in memory: reads go
// through FindByKind + OpenByRelPath, and PutStreamAs writes the request body as it arrives.
type FileStore interface {
	PutStreamAs(id, kind, ext string, r io.Reader, c filestore.Charge) (string, error)
	FindByKind(id, kind string) (string, error)
	OpenByRelPath(rel string) (*os.File, error)
	RemoveKind(id, kind string) error
	Remove(id string) error
}

// LLM proxies chat turns to the configured provider (llm-contract.md §6.3). nil means the proxy is
// disabled: /llm/info reports enabled=false and /llm/chat answers 503 llmDisabled.
type LLM interface {
	Chat(ctx context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error)
	Model() string
}

// upstreamError is the llm package's classified-failure seam (*llm.UpstreamError): an error carrying a
// sanitized, client-safe reason for an upstream condition the user can act on.
type upstreamError interface {
	error
	ClientMessage() string
}
