package hub

// Connection handling: the two listeners (WebSocket, raw TCP), the hello
// handshake, and the two things a connection can be — a project session member
// or a subscriber to the global events feed.

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"log"
	"net"
	"net/http"
	"sync"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// WSHandler returns an http.Handler that upgrades to WebSocket and joins a
// session/feed. The route carries no id; the hello frame selects the target.
func (h *Hub) WSHandler() http.Handler {
	return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		conn, err := transport.AcceptWS(rw, req)
		if err != nil {
			return // Accept already wrote a response
		}
		ctx := withClientIP(req.Context(), h.hello.requestIP(req))
		if err := h.HandleConn(ctx, conn); err != nil {
			log.Printf("hub: ws connection ended: %v", err)
		}
	})
}

// ServeListener accepts TCP connections and handles each as an edit connection (NDJSON framing). It
// blocks until the listener is closed, then waits for in-flight connections to drain.
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
	untrack := h.trackConn(conn, cancel)
	defer untrack()

	// First frame must be a hello within the deadline.
	hctx, hcancel := context.WithTimeout(ctx, helloTimeout)
	raw, err := conn.Read(hctx)
	hcancel()
	if err != nil {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return err
	}
	var hello protocol.WSMessage
	if err := json.Unmarshal(raw, &hello); err != nil || hello.Type != protocol.WSHello {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return err
	}
	if err := h.checkHello(ctx, conn, hello); err != nil {
		return err
	}

	if hello.ProjectID == "" {
		return h.serveEvents(ctx, conn)
	}
	return h.serveProject(ctx, conn, hello)
}

// serveProject registers the connection as a member of a project session and
// pumps frames until it disconnects.
func (h *Hub) serveProject(ctx context.Context, conn transport.Conn, hello protocol.WSMessage) error {
	clientID := hello.ClientID
	if clientID == "" {
		clientID = randomID()
	}
	m := newMember(clientID, hello.Name, conn)

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
	// Unregister BEFORE waiting on the writer: the run-loop's unregister handler closes m.out, which is what
	// lets writeLoop finish. Waiting first would deadlock a member whose out channel is idle.
	select {
	case s.unregister <- m:
	case <-s.done:
	}
	<-writerDone
	return nil
}

// serveEvents subscribes the connection to the global project-lifecycle feed so the client can
// live-update its projects list, forwarding every event until the peer disconnects.
func (h *Hub) serveEvents(ctx context.Context, conn transport.Conn) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	ch, unsub := h.bus.Subscribe(eventbus.ChannelEvents)
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
		case env, ok := <-ch:
			if !ok {
				return nil
			}
			if err := conn.Write(ctx, env.Data); err != nil {
				return err
			}
		}
	}
}

func randomID() string {
	var b [8]byte
	_, _ = rand.Read(b[:])
	return "c_" + hex.EncodeToString(b[:])
}
