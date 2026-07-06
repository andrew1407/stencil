// Package hub runs live collaborative edit sessions. One ProjectSession exists
// per open project id; every connection for that project — over WebSocket or TCP
// — joins the same session. Edits are relayed peer-to-peer through a bus (Redis
// across instances, in-proc otherwise); durable snapshots are persisted on save
// under a last-writer-wins version guard. The session run-loop is the sole owner
// of session state, so there are no shared-memory races (verified with -race).
package hub

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"log"
	"net"
	"net/http"
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

var nowMs = func() int64 { return time.Now().UnixMilli() }

// Hub owns the set of live sessions.
type Hub struct {
	store    Store
	bus      bus.Bus
	resolver auth.SessionResolver
	ctx      context.Context

	mu       sync.Mutex
	sessions map[string]*session
	conns    map[*connReg]struct{} // live connection cancels, guarded by mu
}

// connReg is one tracked live connection; cancelling its context unwinds the
// connection's handler (both transports honor ctx), which is how shutdown drains
// hijacked WebSocket editors that httpSrv.Shutdown cannot reach.
type connReg struct{ cancel context.CancelFunc }

// New constructs a hub. ctx bounds background publishes/persists.
func New(ctx context.Context, store Store, b bus.Bus, resolver auth.SessionResolver) *Hub {
	return &Hub{
		store:    store,
		bus:      b,
		resolver: resolver,
		ctx:      ctx,
		sessions: map[string]*session{},
		conns:    map[*connReg]struct{}{},
	}
}

// trackConn records a live connection's cancel func so CloseAll can reach it,
// returning an untrack func to call when the connection ends.
func (h *Hub) trackConn(cancel context.CancelFunc) func() {
	reg := &connReg{cancel: cancel}
	h.mu.Lock()
	h.conns[reg] = struct{}{}
	h.mu.Unlock()
	return func() {
		h.mu.Lock()
		delete(h.conns, reg)
		h.mu.Unlock()
	}
}

// CloseAll cancels every live connection's context so its handler unwinds and
// releases the conn. It is non-blocking (it only fires cancels); handlers drain
// asynchronously, letting httpSrv.Shutdown and ServeListener's wg.Wait() return.
// Safe to call concurrently with connects/disconnects (guarded by mu).
func (h *Hub) CloseAll() {
	h.mu.Lock()
	for reg := range h.conns {
		reg.cancel()
	}
	h.mu.Unlock()
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

// WSHandler returns an http.Handler that upgrades to WebSocket and joins a
// session/feed. The route carries no id; the hello frame selects the target.
func (h *Hub) WSHandler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := transport.AcceptWS(w, r)
		if err != nil {
			return // Accept already wrote a response
		}
		if err := h.HandleConn(r.Context(), conn); err != nil {
			log.Printf("hub: ws connection ended: %v", err)
		}
	})
}

// ServeListener accepts TCP connections and handles each as an edit connection
// (NDJSON framing). It blocks until the listener is closed, then waits for
// in-flight connections to drain.
func (h *Hub) ServeListener(ln net.Listener) error {
	var wg sync.WaitGroup
	for {
		c, err := ln.Accept()
		if err != nil {
			wg.Wait()
			return err
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := h.HandleConn(h.ctx, transport.NewTCP(c)); err != nil {
				log.Printf("hub: tcp connection ended: %v", err)
			}
		}()
	}
}

// HandleConn performs the hello handshake then routes the connection to either a
// project session or the global events feed. It blocks until the connection ends.
func (h *Hub) HandleConn(ctx context.Context, conn transport.Conn) error {
	// Track this connection so a shutdown CloseAll can cancel it and drain the
	// conn (WebSocket conns are hijacked, so httpSrv.Shutdown can't close them).
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	untrack := h.trackConn(cancel)
	defer untrack()

	// First frame must be a hello within the deadline.
	hctx, cancel := context.WithTimeout(ctx, helloTimeout)
	raw, err := conn.Read(hctx)
	cancel()
	if err != nil {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return err
	}
	var hello protocol.WSMessage
	if err := json.Unmarshal(raw, &hello); err != nil || hello.Type != protocol.WSHello {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return err
	}
	if _, err := auth.Verify(ctx, h.resolver, hello.Token, nowMs()); err != nil {
		writeMsg(ctx, conn, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeUnauthorized, Message: "invalid token"})
		_ = conn.Close(transport.ClosePolicyViolation, "unauthorized")
		return err
	}

	if hello.ProjectID == "" {
		return h.serveEvents(ctx, conn)
	}
	return h.serveProject(ctx, conn, hello)
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

// serveProject registers the connection as a member of a project session and
// pumps frames until it disconnects.
func (h *Hub) serveProject(ctx context.Context, conn transport.Conn, hello protocol.WSMessage) error {
	clientID := hello.ClientID
	if clientID == "" {
		clientID = randomID()
	}
	m := &member{clientID: clientID, name: hello.Name, conn: conn, out: make(chan []byte, outBuffer)}

	s := h.acquire(hello.ProjectID)
	defer h.release(s)

	writerDone := make(chan struct{})
	go m.writeLoop(ctx, writerDone)

	s.register <- m

readLoop:
	for {
		raw, err := conn.Read(ctx)
		if err != nil {
			break
		}
		var msg protocol.WSMessage
		if json.Unmarshal(raw, &msg) != nil {
			continue
		}
		select {
		case s.incoming <- inbound{member: m, msg: msg}:
		case <-s.done:
			break readLoop
		case <-ctx.Done():
			break readLoop
		}
	}
	_ = conn.Close(transport.CloseNormal, "bye")
	// Unregister BEFORE waiting on the writer: the run-loop's unregister handler is
	// what closes m.out, which is what lets writeLoop (and thus writerDone) finish.
	// Waiting first would deadlock a member whose out channel is idle at disconnect.
	// If the session is already tearing down (s.done), run() closes m.out itself.
	select {
	case s.unregister <- m:
	case <-s.done:
	}
	<-writerDone
	return nil
}

// serveEvents subscribes the connection to the global project-lifecycle feed so
// the client can live-update its projects list. It reads (to detect close) and
// forwards every event until the peer disconnects.
func (h *Hub) serveEvents(ctx context.Context, conn transport.Conn) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	ch, unsub := h.bus.Subscribe(bus.ChannelEvents)
	defer unsub()

	// Detect client disconnect by reading; any read error cancels the loop.
	go func() {
		for {
			if _, err := conn.Read(ctx); err != nil {
				cancel()
				return
			}
		}
	}()

	for {
		select {
		case <-ctx.Done():
			_ = conn.Close(transport.CloseNormal, "bye")
			return nil
		case data, ok := <-ch:
			if !ok {
				return nil
			}
			if err := conn.Write(ctx, data); err != nil {
				return err
			}
		}
	}
}

// writeMsg marshals and sends a single message (best effort).
func writeMsg(ctx context.Context, conn transport.Conn, msg protocol.WSMessage) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	_ = conn.Write(ctx, data)
}

func randomID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return "c_" + hex.EncodeToString(b[:])
}
