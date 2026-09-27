package service

import (
	"context"
	"io"
	"time"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// ProjectStore is the persistence the services need. *store.Store satisfies it.
type ProjectStore interface {
	ProjectExists(ctx context.Context, id string) (bool, error)
	OwnerProjectIDs(ctx context.Context, id string) ([]string, error)
	CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error)
	SetFile(ctx context.Context, id string, f store.StoredFile) (protocol.ProjectRecord, error)
	DeleteProject(ctx context.Context, id string) error
}

// ProjectFiles is the byte side of a project's whole lifetime, UploadFiles the byte side of one upload:
// split so a caller with only one of the two jobs (the expiry sweep) need not fake the other.
type ProjectFiles interface {
	Remove(id string) error
}

type UploadFiles interface {
	PutStreamAs(id, kind, ext string, r io.Reader, c filestore.Charge) (string, error)
	RemoveKind(id, kind string) error
}

// ChargeLedger records which session wrote each stored file (STORAGE_QUOTA_PER_SESSION_BYTES).
// *store.Store satisfies it; only a service with that cap on ever calls it.
type ChargeLedger interface {
	Admit(ctx context.Context, c store.Charge, fits func(held int64) error, commit func() error) error
	Credit(ctx context.Context, projectID, kind string) error
}

// SessionCounter reports how many clients are in a project's live edit session.
// The hub satisfies it; nil means "treat every project as having none".
type SessionCounter interface {
	ConnectionCount(projectID string) int
}

// ExpiredStore is the sweep's query: expired rows, minus the ids to keep. *store.Store satisfies it.
type ExpiredStore interface {
	DeleteExpiredProjects(ctx context.Context, now int64, limit int, keep []string) ([]string, error)
}

// LiveProjects lists every project with a live edit session. The hub satisfies it.
type LiveProjects interface {
	LiveProjectIDs() []string
}

// RemoteLive lists the projects live on the other instances sharing the database. *Presence satisfies it.
type RemoteLive interface {
	LiveElsewhere(ctx context.Context) ([]string, error)
}

// RemoteSessions counts a project's live editors on the other instances. *Presence satisfies it;
// nil means this instance is the only one.
type RemoteSessions interface {
	RemoteMembers(ctx context.Context, projectID string) (int, error)
}

// PresenceStore is the shared liveness table. *store.Store satisfies it.
type PresenceStore interface {
	BeatPresence(ctx context.Context, instance string, live map[string]int, ttl time.Duration) error
	LiveElsewhere(ctx context.Context, instance string) ([]string, error)
	MembersElsewhere(ctx context.Context, instance, projectID string) (int, error)
}

// LiveCounts maps each project live on this instance to its member count. The hub satisfies it.
type LiveCounts interface {
	LiveCounts() map[string]int
}
