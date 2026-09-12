// Package hub runs live collaborative edit sessions. One ProjectSession exists
// per open project id; every connection for that project — over WebSocket or TCP
// — joins the same session. Edits are relayed peer-to-peer through a bus (Redis
// across instances, in-proc otherwise); durable snapshots are persisted on save
// under a last-writer-wins version guard. The session run-loop is the sole owner
// of session state, so there are no shared-memory races (verified with -race).
package hub

import (
	"context"
	"sync"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
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
	bus      bus.Bus
	resolver auth.SessionResolver
	ctx      context.Context
	hello    helloGuard // per-IP throttle on failed handshakes (hellolimit.go)

	mu       sync.Mutex
	sessions map[string]*session
	conns    map[*connReg]struct{} // live connection cancels, guarded by mu
}

// connReg is one tracked live connection; cancelling its context unwinds the
// connection's handler (both transports honor ctx), which is how shutdown drains
// hijacked WebSocket editors that httpSrv.Shutdown cannot reach. The conn itself
// is kept so shutdown can send the notice before cancelling.
type connReg struct {
	cancel context.CancelFunc
	conn   transport.Conn
}

// New constructs a hub. ctx bounds background publishes/persists; opts carry the
// tunables (WithHelloLimit).
func New(ctx context.Context, store Store, b bus.Bus, resolver auth.SessionResolver, opts ...Option) *Hub {
	h := &Hub{
		store:    store,
		bus:      b,
		resolver: resolver,
		ctx:      ctx,
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

// CloseAll tells every live connection the server is going away (unsaved live
// edits die with it — shutdown.go) and cancels its context so the handler
// unwinds and releases the conn.
func (h *Hub) CloseAll() {
	closeAll(h.liveConns())
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

// ConnectionCount returns how many clients are currently in project id's live edit
// session (0 when none). It reads the session refcount under the hub lock, so it is
// safe to call from the REST delete handler.
func (h *Hub) ConnectionCount(projectID string) int {
	h.mu.Lock()
	defer h.mu.Unlock()
	if s := h.sessions[projectID]; s != nil {
		return s.refs
	}
	return 0
}
