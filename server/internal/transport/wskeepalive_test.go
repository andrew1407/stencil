package transport

// The WebSocket upgrade itself, and the keepalive ping that reaps a peer which
// has stopped answering while sparing one that has not.

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func TestDialWSFailsAgainstANonWebSocketEndpoint(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK) // a plain 200, no upgrade
	}))
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if c, err := dialWS(ctx, "ws"+strings.TrimPrefix(srv.URL, "http")); err == nil {
		c.Close(CloseNormal, "")
		t.Fatal("expected dialWS to fail without an upgrade")
	}
}

// Origin checking is deliberately off (InsecureSkipVerify): a client is authenticated by the in-band hello
// token, so a cross-origin handshake must upgrade cleanly. The Go dialer sends no Origin, hence the library.
func TestAcceptWSUpgradesFromAForeignOrigin(t *testing.T) {
	accepted := make(chan error, 1)
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		c, err := AcceptWS(rw, req)
		accepted <- err
		if err == nil {
			// The client hard-closes first (below), so this never waits on a
			// handshake reply.
			defer c.Close(CloseNormal, "")
			<-release
		}
	}))
	t.Cleanup(srv.Close)
	t.Cleanup(func() { close(release) })

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	c, _, err := websocket.Dial(ctx, "ws"+strings.TrimPrefix(srv.URL, "http"), &websocket.DialOptions{
		HTTPHeader: http.Header{"Origin": []string{"https://not-the-server.example"}},
	})
	if err != nil {
		t.Fatalf("a cross-origin handshake was refused: %v", err)
	}
	t.Cleanup(func() { c.CloseNow() })
	if err := <-accepted; err != nil {
		t.Fatalf("AcceptWS refused the upgrade: %v", err)
	}
}

// A silent WS peer (never reads, so never pongs) must be reaped within the keepalive window — WS conns are
// hijacked, so nothing else bounds a half-open peer. Shorten the vars BEFORE the pair is created.
func TestWSKeepaliveReapsASilentPeer(t *testing.T) {
	prevI, prevT := wsPingInterval, wsPongTimeout
	wsPingInterval, wsPongTimeout = 50*time.Millisecond, 100*time.Millisecond
	t.Cleanup(func() { wsPingInterval, wsPongTimeout = prevI, prevT })

	_, server := wsPair(t) // the client end never reads, so pings go unanswered

	start := time.Now()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); err == nil {
		t.Fatal("expected the read to fail once the silent peer was reaped")
	} else if ctx.Err() != nil {
		t.Fatalf("keepalive never reaped the silent peer: %v", err)
	}
	// interval + pong timeout + generous scheduling slack.
	if elapsed := time.Since(start); elapsed > 2*time.Second {
		t.Errorf("silent peer reaped after %v, want ~150ms", elapsed)
	}
}

// An active peer must NOT be reaped: while both ends keep a Read pending (as the hub does), pings are
// answered and pongs consumed, and traffic still flows after several keepalive rounds.
func TestWSKeepaliveSparesAnActivePeer(t *testing.T) {
	prevI, prevT := wsPingInterval, wsPongTimeout
	wsPingInterval, wsPongTimeout = 50*time.Millisecond, 200*time.Millisecond
	t.Cleanup(func() { wsPingInterval, wsPongTimeout = prevI, prevT })

	client, server := wsPair(t)

	read := func(c Conn) chan []byte {
		out := make(chan []byte, 1)
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			if data, err := c.Read(ctx); err == nil {
				out <- data
			}
			close(out)
		}()
		return out
	}
	serverGot, clientGot := read(server), read(client)

	time.Sleep(500 * time.Millisecond) // ~10 ping rounds while both ends read

	write1(t, client, `{"type":"ping"}`)
	if got, ok := <-serverGot; !ok || string(got) != `{"type":"ping"}` {
		t.Fatalf("server read after the idle window failed: %q ok=%v", got, ok)
	}
	write1(t, server, `{"type":"pong"}`)
	if got, ok := <-clientGot; !ok || string(got) != `{"type":"pong"}` {
		t.Fatalf("client read after the idle window failed: %q ok=%v", got, ok)
	}
}
