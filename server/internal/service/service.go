// Package service holds the server's application policy: the rules that must
// hold however a project is reached, and the multi-store sequences no transport
// should re-implement. The REST handlers and the expiry sweep both drive these,
// so a rule cannot be enforced on one path and missed on another.
//
// It owns no transport concepts: no ResponseWriter, no status codes. Failures
// come back as the sentinels below (plus the store's own ErrNotFound), and each
// caller maps them onto its own wire.
package service

import (
	"context"
	"errors"
	"io"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// Policy failures. Everything else is the underlying store's error, passed
// through so a caller can still tell a missing project from a broken one.
var (
	// ErrImageRequired: a Stencil project is created FROM an image.
	ErrImageRequired = errors.New("service: a project must be created from an image")
	// ErrProjectInUse: two or more clients share the live edit session.
	ErrProjectInUse = errors.New("service: project is in use by other clients")
	// ErrRecordFile: the bytes landed but the project row did not take them.
	ErrRecordFile = errors.New("service: could not record the stored file")
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

// announce publishes a project-lifecycle event on the global feed.
func announce(ctx context.Context, b bus.Bus, event string, rec protocol.ProjectRecord) {
	bus.PublishProjectEvent(ctx, b, event, rec)
}

// isMissing reports whether err says the project is gone.
func isMissing(err error) bool { return errors.Is(err, store.ErrNotFound) }
