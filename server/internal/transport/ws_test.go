// WebSocket adapter: the same Conn contract as TCP, plus the upgrade itself and
// the keepalive that reaps a silent peer.
package transport

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
)

// wsPair runs an httptest server that upgrades one request, and returns the
// dialled client end plus the accepted server end.
func wsPair(t *testing.T) (client, server Conn) {
	t.Helper()
	accepted := make(chan Conn, 1)
	// The handler must not return while the test is using the connection —
	// returning would tear the upgraded conn down — so it parks on `release`.
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, err := AcceptWS(w, r)
		if err != nil {
			close(accepted)
			return
		}
		accepted <- c
		<-release
	}))
	// Cleanups run LIFO, so these register outermost-first: srv.Close runs last,
	// after the handler has been released. Parking on r.Context().Done() instead
	// would never fire (an upgraded conn is hijacked and detached from the
	// server's request tracking), leaving srv.Close to wait out its full 5s grace
	// period on every WS test.
	t.Cleanup(srv.Close)
	t.Cleanup(func() { close(release) })

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	c, err := dialWS(ctx, "ws"+strings.TrimPrefix(srv.URL, "http"))
	if err != nil {
		t.Fatal(err)
	}
	s, ok := <-accepted
	if !ok {
		t.Fatal("upgrade failed")
	}
	// A WebSocket close is a handshake: Close writes its close frame and then
	// waits for the peer's reply. Closing the two ends one after another leaves
	// the first with nobody to reply to, so it burns its full 5s timeout on every
	// test. Closing them concurrently means each is reading while the other
	// writes, and both return at once.
	t.Cleanup(func() {
		var wg sync.WaitGroup
		wg.Add(2)
		go func() { defer wg.Done(); c.Close(CloseNormal, "") }()
		go func() { defer wg.Done(); s.Close(CloseNormal, "") }()
		wg.Wait()
	})
	return c, s
}

// Both adapters deliver identical protocol messages, so a desktop (TCP) and a
// browser (WS) client editing one project land in the same hub session.
func TestWSRoundTripsOneMessagePerReadWrite(t *testing.T) {
	client, server := wsPair(t)
	write1(t, client, `{"type":"hello"}`)
	if got := string(read1(t, server)); got != `{"type":"hello"}` {
		t.Errorf("got %q", got)
	}
	write1(t, server, `{"type":"welcome"}`)
	if got := string(read1(t, client)); got != `{"type":"welcome"}` {
		t.Errorf("got %q", got)
	}
}

// Frames are message-oriented already, but the boundary must survive a burst —
// the hub relies on never seeing two messages fused.
func TestWSPreservesMessageBoundaries(t *testing.T) {
	client, server := wsPair(t)
	for _, m := range []string{`{"n":1}`, `{"n":2}`, `{"n":3}`} {
		write1(t, client, m)
	}
	for _, want := range []string{`{"n":1}`, `{"n":2}`, `{"n":3}`} {
		if got := string(read1(t, server)); got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestWSReadUnblocksOnContextCancel(t *testing.T) {
	_, server := wsPair(t)
	ctx, cancel := context.WithCancel(context.Background())

	done := make(chan error, 1)
	go func() {
		_, err := server.Read(ctx)
		done <- err
	}()

	time.Sleep(50 * time.Millisecond)
	cancel()

	select {
	case err := <-done:
		if err == nil {
			t.Error("expected a read error on cancellation")
		}
	case <-time.After(3 * time.Second):
		t.Fatal("Read did not unblock on cancellation")
	}
}

// The same memory guard as TCP, enforced by the library's read limit.
func TestWSRejectsAnOverLimitFrame(t *testing.T) {
	client, server := wsPair(t)
	go func() {
		huge := make([]byte, MaxMessageBytes+1024)
		for i := range huge {
			huge[i] = 'a'
		}
		_ = client.Write(context.Background(), huge)
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); err == nil {
		t.Fatal("expected an over-limit frame to fail the read")
	} else if ctx.Err() != nil {
		t.Fatalf("read timed out instead of rejecting the frame: %v", err)
	}
}

func TestWSReadFailsAfterThePeerCloses(t *testing.T) {
	client, server := wsPair(t)

	// A WebSocket close is a handshake, so the peer must be reading for it to
	// complete — start the read first, then close underneath it.
	done := make(chan error, 1)
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_, err := server.Read(ctx)
		done <- err
	}()
	time.Sleep(50 * time.Millisecond)

	if err := client.Close(CloseNormal, "bye"); err != nil {
		t.Fatalf("close: %v", err)
	}
	select {
	case err := <-done:
		if err == nil {
			t.Fatal("expected a read error once the peer closed")
		}
	case <-time.After(5 * time.Second):
		t.Fatal("the read did not observe the peer's close")
	}
}

func TestWSRemoteAddrIdentifiesThePeer(t *testing.T) {
	_, server := wsPair(t)
	if addr := server.RemoteAddr(); addr == "" {
		t.Error("RemoteAddr() is empty; it is the only peer identifier in the logs")
	}
}
