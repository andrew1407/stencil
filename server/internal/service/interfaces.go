package service

import (
	"context"
	"io"

	"stencil/server/internal/protocol"
)

// ProjectStore is the persistence the services need. *store.Store satisfies it.
type ProjectStore interface {
	GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error)
	CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error)
	SetFile(ctx context.Context, id, kind, relPath string, w, h int) (protocol.ProjectRecord, error)
	DeleteProject(ctx context.Context, id string) error
}

// ProjectFiles is the byte side of a project's whole lifetime; UploadFiles the
// byte side of one upload. Split so a caller with only one of the two jobs (the
// expiry sweep) need not fake the other. *filestore.Store satisfies both.
type ProjectFiles interface {
	Remove(id string) error
}

type UploadFiles interface {
	PutStream(id, kind, ext string, r io.Reader) (string, error)
	RemoveKind(id, kind string) error
}

// SessionCounter reports how many clients are in a project's live edit session.
// The hub satisfies it; nil means "treat every project as having none".
type SessionCounter interface {
	ConnectionCount(projectID string) int
}
