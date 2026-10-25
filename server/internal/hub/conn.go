package hub

// Connection handling: the two listeners (WebSocket, raw TCP), the hello handshake, and the global
// events feed a connection listens to afterwards.

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// WSHandler returns an http.Handler that upgrades to WebSocket and serves the feed.
func (h *Hub) WSHandler() http.Handler {
	return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		// Refused before the upgrade, so a hostile page spends neither a per-IP slot nor a hello token.
		if origin := req.Header.Get("Origin"); h.originOK != nil && !sameOrigin(origin, req) && !h.originOK(origin) {
			http.Error(rw, "origin not allowed", http.StatusForbidden)
			return
		}
		conn, err := transport.AcceptWS(rw, req)
		if err != nil {
			return // Accept already wrote a response
		}
		ctx := withClientIP(req.Context(), h.hello.requestIP(req))
		if err := h.HandleConn(ctx, conn); err != nil && !h.refusals.refused(err, time.Now()) {
			log.Printf("hub: ws connection ended: %v", err)
		}
	})
}

// sameOrigin reports a page served from the host the upgrade names (directly, or through a proxy's
// X-Forwarded-Host): same-origin REST needs no CORS_ORIGINS entry, so neither does its live feed.
func sameOrigin(origin string, req *http.Request) bool {
	u, err := url.Parse(origin)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
		return false
	}
	for _, host := range []string{req.Host, strings.TrimSpace(strings.Split(req.Header.Get("X-Forwarded-Host"), ",")[0])} {
		if host != "" && strings.EqualFold(u.Host, host) {
			return true
		}
	}
	return false
}

// ServeListener accepts TCP connections and handles each as a feed connection (NDJSON framing). Any
// accept error but a closed listener (EMFILE, ECONNABORTED) is logged and retried after a capped backoff,
// as net/http does; once the listener is closed it waits for in-flight connections to drain.
func (h *Hub) ServeListener(ln net.Listener) error {
	var wg sync.WaitGroup
	var backoff time.Duration
	for {
		c, err := ln.Accept()
		if errors.Is(err, net.ErrClosed) {
			wg.Wait()
			return err
		}
		if err != nil {
			backoff = min(max(2*backoff, acceptBackoffMin), acceptBackoffMax)
			log.Printf("hub: tcp accept: %v; retrying in %v", err, backoff)
			select {
			case <-time.After(backoff):
				continue
			case <-h.ctx.Done():
				wg.Wait()
				return err
			}
		}
		backoff = 0
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := h.HandleConn(h.ctx, transport.NewTCP(c)); err != nil && !h.refusals.refused(err, time.Now()) {
				log.Printf("hub: tcp connection ended: %v", err)
			}
		}()
	}
}

// The accept retry starts at acceptBackoffMin and doubles to acceptBackoffMax, net/http's own bounds.
var (
	acceptBackoffMin = 5 * time.Millisecond
	acceptBackoffMax = time.Second
)

// HandleConn takes the per-IP slot, performs the hello handshake and serves the global events feed. It
// blocks until the connection ends.
func (h *Hub) HandleConn(ctx context.Context, conn transport.Conn) error {
	// A failed feed write is the one return that has not hung up yet; a second Close is a no-op.
	defer func() { _ = conn.Close(transport.CloseNormal, "bye") }()
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	reg, untrack := h.trackConn(conn, cancel)
	defer untrack()

	ip := h.hello.connIP(ctx, conn)
	if err := h.perIP.take(ip); err != nil {
		msg := "too many connections from this address"
		if errors.Is(err, errServerFull) {
			msg = "the server is at its connection limit"
		}
		refuseHello(ctx, conn, protocol.CodeRateLimited, msg)
		return err
	}
	defer h.perIP.drop(ip)

	hello, err := h.readHello(ctx, conn)
	if err != nil {
		return err
	}
	sess, err := h.checkHello(ctx, conn, hello)
	if err != nil {
		return err
	}
	if err := checkHelloFields(ctx, conn, hello); err != nil {
		return err
	}
	h.authenticated(reg, sess.ID)
	defer h.expireAt(sess.ExpiresAt, conn, cancel)()
	return h.serveEvents(ctx, conn)
}

// readHello reads the first frame under the hello deadline and the hello read limit; anything but a
// hello closes the connection.
func (h *Hub) readHello(ctx context.Context, conn transport.Conn) (protocol.WSMessage, error) {
	hctx, hcancel := context.WithTimeout(ctx, h.tune.HelloTimeout)
	raw, err := conn.Read(hctx)
	hcancel()
	if err != nil {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return protocol.WSMessage{}, err
	}
	var hello protocol.WSMessage
	if err := json.Unmarshal(raw, &hello); err != nil || hello.Type != protocol.WSHello {
		_ = conn.Close(transport.ClosePolicyViolation, "expected hello")
		return protocol.WSMessage{}, err
	}
	return hello, nil
}

// checkHelloFields refuses a hello that names a project (no session is served), after the token was good
// so it spends no hello budget.
func checkHelloFields(ctx context.Context, conn transport.Conn, hello protocol.WSMessage) error {
	if hello.ProjectID != "" {
		refuseHello(ctx, conn, protocol.CodeBadRequest, "project sessions are not served; omit projectId for the events feed")
		return errProjectHello
	}
	return nil
}

// serveEvents subscribes the connection to the global project-lifecycle feed so the client can
// live-update its projects list, forwarding every event until the peer disconnects.
func (h *Hub) serveEvents(ctx context.Context, conn transport.Conn) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	ch, unsub := h.feed.listen()
	defer unsub()

	// Inbound frames are discarded under the hello cap; any read error (one past the cap too) ends the feed.
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
				_ = conn.Close(transport.CloseNormal, "feed ended")
				return nil
			}
			if err := conn.Write(ctx, data); err != nil {
				return err
			}
		}
	}
}
