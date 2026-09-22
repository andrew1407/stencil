// Package hub runs live collaborative edit sessions: one session per open project id, joined by every
// WebSocket and TCP connection for that project. Edits relay through a bus (Redis across instances,
// in-proc otherwise); snapshots persist on save under a last-writer-wins version guard. The session
// run-loop is the sole owner of session state.
package hub

import (
	"context"
	"sync"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/transport"
)

// Store is the project persistence the hub needs for snapshots.
type Store interface {
	GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error)
	UpdateProject(ctx context.Context, id string, patch store.ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error)
}

const (
	outBuffer = 256
	opTimeout = 5 * time.Second
)

// helloTimeout bounds how long a fresh connection may take to send its hello
// frame. A var (not const) so tests can shorten it.
var helloTimeout = 10 * time.Second

// Hub owns the set of live sessions.
type Hub struct {
	store    Store
	bus      eventbus.Bus
	resolver auth.SessionResolver
	ctx      context.Context
	cancel   context.CancelFunc
	hello    helloGuard // per-IP throttle on failed handshakes (hellolimit.go)

	mu       sync.Mutex
	sessions map[string]*session
	conns    map[*connReg]struct{} // live connection cancels, guarded by mu
}

// connReg is one tracked live connection; cancelling its context unwinds the handler (both transports
// honor ctx), draining hijacked WebSocket editors Shutdown cannot reach. The conn is kept for the notice.
type connReg struct {
	cancel context.CancelFunc
	conn   transport.Conn
}

// New constructs a hub. Its own context carries ctx's values but not its cancellation, so a signal that
// cancels ctx cannot hang up a TCP editor before CloseAll says goodbye; Close ends it. opts are tunables.
func New(ctx context.Context, store Store, b eventbus.Bus, resolver auth.SessionResolver, opts ...Option) *Hub {
	hctx, cancel := context.WithCancel(context.WithoutCancel(ctx))
	h := &Hub{
		store:    store,
		bus:      b,
		resolver: resolver,
		ctx:      hctx,
		cancel:   cancel,
		sessions: map[string]*session{},
		conns:    map[*connReg]struct{}{},
	}
	for _, opt := range opts {
		opt(h)
	}
	return h
}

// trackConn records a live connection's cancel func so CloseAll can reach it,
// returning an untrack func to call when the connection ends.
func (h *Hub) trackConn(conn transport.Conn, cancel context.CancelFunc) func() {
	reg := &connReg{cancel: cancel, conn: conn}
	h.mu.Lock()
	h.conns[reg] = struct{}{}
	h.mu.Unlock()
	return func() {
		h.mu.Lock()
		delete(h.conns, reg)
		h.mu.Unlock()
	}
}

// acquire returns the session for id, creating and starting it if needed, and
// increments its reference count. Each acquire must be paired with release.
func (h *Hub) acquire(id string) *session {
	h.mu.Lock()
	defer h.mu.Unlock()
	s := h.sessions[id]
	if s == nil {
		s = newSession(h, id)
		h.sessions[id] = s
		go s.run()
	}
	s.refs++
	return s
}

// release drops a reference; when the last one goes the session is removed and
// its run-loop stopped.
func (h *Hub) release(s *session) {
	h.mu.Lock()
	defer h.mu.Unlock()
	s.refs--
	if s.refs <= 0 {
		delete(h.sessions, s.id)
		close(s.done)
	}
}

// ConnectionCount returns how many clients are in project id's live edit session (0 when none). It reads
// the session refcount under the hub lock, so the REST delete handler may call it.
func (h *Hub) ConnectionCount(projectID string) int {
	h.mu.Lock()
	defer h.mu.Unlock()
	if s := h.sessions[projectID]; s != nil {
		return s.refs
	}
	return 0
}
