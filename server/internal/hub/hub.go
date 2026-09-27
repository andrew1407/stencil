// Package hub runs live collaborative edit sessions: one session per open project id, joined by every
// WebSocket and TCP connection for that project. Edits relay through a bus (Redis across instances,
// in-proc otherwise); snapshots persist on save under a last-writer-wins version guard. The session
// run-loop is the sole owner of session state.
package hub

import (
	"context"
	"sync"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/transport"
)

// Store is the project persistence the hub needs: the join check and the session's snapshots, which carry
// the layout but never the original image.
type Store interface {
	ProjectExists(ctx context.Context, id string) (bool, error)
	GetProjectSnapshot(ctx context.Context, id string) (protocol.ProjectRecord, error)
	UpdateProject(ctx context.Context, id string, patch store.ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error)
}

// Hub owns the set of live sessions.
type Hub struct {
	store    Store
	bus      eventbus.Bus
	resolver auth.SessionResolver
	ctx      context.Context
	cancel   context.CancelFunc
	hello    helloGuard // per-IP throttle on failed handshakes (hellolimit.go)
	tune     Tuning     // queues and deadlines (tuning.go)

	mu       sync.Mutex
	sessions map[string]*session
	conns    map[*connReg]struct{} // live connection cancels, guarded by mu
	changed  chan struct{}         // one pending signal: a session's member count moved
}

// connReg is one tracked live connection; cancelling its context unwinds the handler (both transports
// honor ctx), draining hijacked WebSocket editors Shutdown cannot reach. The conn is kept for the notice.
type connReg struct {
	cancel context.CancelFunc
	conn   transport.Conn
}

// New constructs a hub and subscribes it to the global feed (feed.go). Its own context carries ctx's values
// but not its cancellation, so a signal cannot hang up a TCP editor before CloseAll says goodbye; Close ends it.
func New(ctx context.Context, store Store, b eventbus.Bus, resolver auth.SessionResolver, opts ...Option) *Hub {
	hctx, cancel := context.WithCancel(context.WithoutCancel(ctx))
	h := &Hub{
		store:    store,
		bus:      b,
		resolver: resolver,
		ctx:      hctx,
		cancel:   cancel,
		tune:     defaultTuning,
		sessions: map[string]*session{},
		conns:    map[*connReg]struct{}{},
		changed:  make(chan struct{}, 1),
	}
	for _, opt := range opts {
		opt(h)
	}
	if b != nil {
		events, stop := b.Subscribe(eventbus.ChannelEvents)
		go h.watchFeed(events, stop)
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

// acquire returns the session for id, creating it if needed, and takes a reference; pair it with release.
// The bus subscription (a Redis round trip) runs after h.mu is dropped, so it stalls only this project.
func (h *Hub) acquire(id string) *session {
	h.mu.Lock()
	s := h.sessions[id]
	if s == nil {
		s = newSession(h, id)
		h.sessions[id] = s
	}
	s.refs++
	h.signalChanged()
	h.mu.Unlock()
	s.start()
	return s
}

// release drops a reference; when the last one goes the session is removed and
// its run-loop stopped.
func (h *Hub) release(s *session) {
	h.mu.Lock()
	defer h.mu.Unlock()
	s.refs--
	h.signalChanged()
	if s.refs <= 0 {
		delete(h.sessions, s.id)
		close(s.done)
	}
}

// signalChanged leaves one pending signal on LiveChanged; later ones coalesce into it.
func (h *Hub) signalChanged() {
	select {
	case h.changed <- struct{}{}:
	default:
	}
}

// LiveChanged fires after a join or a leave, so the presence heartbeat can publish it at once.
func (h *Hub) LiveChanged() <-chan struct{} { return h.changed }

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

// LiveCounts maps each project with a live session on this instance to its member count.
func (h *Hub) LiveCounts() map[string]int {
	h.mu.Lock()
	defer h.mu.Unlock()
	counts := make(map[string]int, len(h.sessions))
	for id, s := range h.sessions {
		counts[id] = s.refs
	}
	return counts
}

// LiveProjectIDs lists the projects with a live session on this instance, for the expiry sweep to defer.
func (h *Hub) LiveProjectIDs() []string {
	h.mu.Lock()
	defer h.mu.Unlock()
	ids := make([]string, 0, len(h.sessions))
	for id := range h.sessions {
		ids = append(ids, id)
	}
	return ids
}
