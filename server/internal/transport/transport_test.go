package transport

import (
	"context"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/coder/websocket"
)

// The hub is written against Conn, so both adapters must behave identically:
// one message per Read/Write, a hard size cap, and a Read that unblocks on
// context cancellation. The TCP adapter carries the extra weight — it hand-rolls
// NDJSON framing so the Zig CLI and Qt desktop need no WebSocket library — and
// that framing is what these tests mostly pin down.

// tcpPair returns two ends of a live TCP connection, both wrapped as Conn.
func tcpPair(t *testing.T) (client, server Conn) {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	accepted := make(chan net.Conn, 1)
	go func() {
		c, err := ln.Accept()
		if err != nil {
			close(accepted)
			return
		}
		accepted <- c
	}()

	c, err := dialTCP(ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(CloseNormal, "") })

	sc, ok := <-accepted
	if !ok {
		t.Fatal("accept failed")
	}
	s := NewTCP(sc)
	t.Cleanup(func() { s.Close(CloseNormal, "") })
	return c, s
}

// read1 reads one message with a short deadline so a hung test fails fast.
func read1(t *testing.T, c Conn) []byte {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	data, err := c.Read(ctx)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	return data
}

func write1(t *testing.T, c Conn, s string) {
	t.Helper()
	if err := c.Write(context.Background(), []byte(s)); err != nil {
		t.Fatalf("write: %v", err)
	}
}

func TestTCPRoundTripsOneMessagePerReadWrite(t *testing.T) {
	client, server := tcpPair(t)
	write1(t, client, `{"type":"hello"}`)
	if got := string(read1(t, server)); got != `{"type":"hello"}` {
		t.Errorf("got %q", got)
	}
	// And back the other way — Conn is bidirectional.
	write1(t, server, `{"type":"welcome"}`)
	if got := string(read1(t, client)); got != `{"type":"welcome"}` {
		t.Errorf("got %q", got)
	}
}

// The delimiter is the frame boundary, not the packet boundary: several messages
// can arrive in one TCP segment and must come back out one at a time.
func TestTCPSplitsCoalescedMessages(t *testing.T) {
	client, server := tcpPair(t)
	raw, ok := client.(*tcpConn)
	if !ok {
		t.Fatal("expected a *tcpConn")
	}
	if _, err := raw.conn.Write([]byte("{\"n\":1}\n{\"n\":2}\n{\"n\":3}\n")); err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{`{"n":1}`, `{"n":2}`, `{"n":3}`} {
		if got := string(read1(t, server)); got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

// The mirror case: one message split across writes is reassembled, so a client
// that flushes mid-message is not misread as two frames.
func TestTCPReassemblesASplitMessage(t *testing.T) {
	client, server := tcpPair(t)
	raw := client.(*tcpConn)
	for _, chunk := range []string{`{"type":"e`, `dit","op":"ro`, `tate"}`} {
		if _, err := raw.conn.Write([]byte(chunk)); err != nil {
			t.Fatal(err)
		}
		time.Sleep(5 * time.Millisecond) // force separate segments
	}
	if _, err := raw.conn.Write([]byte("\n")); err != nil {
		t.Fatal(err)
	}
	if got := string(read1(t, server)); got != `{"type":"edit","op":"rotate"}` {
		t.Errorf("got %q", got)
	}
}

// Write appends the delimiter itself; callers pass bare compact JSON.
func TestTCPWriteAppendsExactlyOneNewline(t *testing.T) {
	client, server := tcpPair(t)
	write1(t, client, `{"a":1}`)
	write1(t, client, `{"b":2}`)
	// If Write emitted no delimiter the two would merge; if it emitted two, the
	// reader would surface an empty frame between them.
	if got := string(read1(t, server)); got != `{"a":1}` {
		t.Fatalf("first frame %q", got)
	}
	if got := string(read1(t, server)); got != `{"b":2}` {
		t.Fatalf("second frame %q", got)
	}
}

// Scanner reuses its internal buffer, so Read must hand back a copy — otherwise
// a caller holding frame N would see it mutate into frame N+1.
func TestTCPReadReturnsAnIndependentCopy(t *testing.T) {
	client, server := tcpPair(t)
	write1(t, client, `{"n":"first-message-padded"}`)
	first := read1(t, server)
	write1(t, client, `{"n":"second-message-pad"}`)
	second := read1(t, server)
	if string(first) != `{"n":"first-message-padded"}` {
		t.Errorf("the first frame was clobbered by the second: %q", first)
	}
	if string(second) != `{"n":"second-message-pad"}` {
		t.Errorf("second frame %q", second)
	}
}

// A frame beyond MaxMessageBytes must fail the read rather than buffer without
// bound — this is the memory guard against a hostile or buggy peer.
func TestTCPRejectsAnOverLimitFrame(t *testing.T) {
	client, server := tcpPair(t)
	raw := client.(*tcpConn)
	go func() {
		huge := make([]byte, MaxMessageBytes+1024)
		for i := range huge {
			huge[i] = 'a'
		}
		_, _ = raw.conn.Write(append(huge, '\n'))
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); err == nil {
		t.Fatal("expected an over-limit frame to fail the read")
	} else if ctx.Err() != nil {
		t.Fatalf("read timed out instead of rejecting the frame: %v", err)
	}
}

// A frame at exactly the cap is still legal — the boundary must not be off by one.
func TestTCPAcceptsAFrameAtTheLimit(t *testing.T) {
	client, server := tcpPair(t)
	raw := client.(*tcpConn)
	payload := strings.Repeat("a", MaxMessageBytes)
	go func() { _, _ = raw.conn.Write(append([]byte(payload), '\n')) }()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	got, err := server.Read(ctx)
	if err != nil {
		t.Fatalf("a frame of exactly MaxMessageBytes was rejected: %v", err)
	}
	if len(got) != MaxMessageBytes {
		t.Errorf("read %d bytes, want %d", len(got), MaxMessageBytes)
	}
}

// The hub's CloseAll cancels each connection's context to unwind a handler
// blocked in Read. bufio.Scanner has no context awareness, so the adapter pokes
// the read deadline; the caller must still see the context error.
func TestTCPReadUnblocksOnContextCancel(t *testing.T) {
	_, server := tcpPair(t)
	ctx, cancel := context.WithCancel(context.Background())

	done := make(chan error, 1)
	go func() {
		_, err := server.Read(ctx)
		done <- err
	}()

	time.Sleep(50 * time.Millisecond) // let the Read block in Scan
	cancel()

	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Errorf("got %v, want context.Canceled", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("Read did not unblock on cancellation")
	}
}

// A context that is already cancelled must not deliver a message.
func TestTCPReadWithAnAlreadyCancelledContext(t *testing.T) {
	client, server := tcpPair(t)
	write1(t, client, `{"type":"ping"}`)
	time.Sleep(50 * time.Millisecond) // the bytes are sitting in the socket

	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := server.Read(ctx); !errors.Is(err, context.Canceled) {
		t.Errorf("got %v, want context.Canceled", err)
	}
}

// A per-call deadline is honoured — this is what bounds the hub's hello timeout.
// The error is context.DeadlineExceeded when the context observes its own expiry
// first, but the adapter pushes the deadline down to the socket, so the raw
// os.ErrDeadlineExceeded can surface instead when the socket trips marginally
// earlier. Both are timeouts; callers only branch on err != nil, so the test
// pins the timing guarantee and accepts either shape rather than the race.
func TestTCPReadHonoursAPerCallDeadline(t *testing.T) {
	_, server := tcpPair(t)
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()

	start := time.Now()
	_, err := server.Read(ctx)
	if err == nil {
		t.Fatal("expected the read to fail at its deadline")
	}
	if !errors.Is(err, context.DeadlineExceeded) && !errors.Is(err, os.ErrDeadlineExceeded) {
		t.Errorf("got %v, want a deadline error", err)
	}
	if elapsed := time.Since(start); elapsed > 2*time.Second {
		t.Errorf("read took %v, want ~100ms", elapsed)
	}
}

// With no per-call deadline a Read still cannot block forever: a wedged or
// vanished peer is reaped once tcpIdleTimeout elapses. The production value is
// generous, so the test shortens the var (which exists for exactly this).
func TestTCPReadTimesOutAnIdlePeer(t *testing.T) {
	prev := tcpIdleTimeout
	tcpIdleTimeout = 100 * time.Millisecond
	t.Cleanup(func() { tcpIdleTimeout = prev })

	_, server := tcpPair(t)
	start := time.Now()
	_, err := server.Read(context.Background())
	if err == nil {
		t.Fatal("expected an idle Read to time out")
	}
	if elapsed := time.Since(start); elapsed > 3*time.Second {
		t.Errorf("idle read took %v, want ~100ms", elapsed)
	}
}

// A live peer is not reaped: traffic inside the idle window keeps the connection
// up, because each Read re-arms the deadline.
func TestTCPIdleTimeoutIsPerReadNotPerConnection(t *testing.T) {
	prev := tcpIdleTimeout
	tcpIdleTimeout = 300 * time.Millisecond
	t.Cleanup(func() { tcpIdleTimeout = prev })

	client, server := tcpPair(t)
	for i := 0; i < 3; i++ {
		time.Sleep(150 * time.Millisecond) // inside the window each time
		write1(t, client, `{"type":"ping"}`)
		if got := string(read1(t, server)); got != `{"type":"ping"}` {
			t.Fatalf("frame %d: got %q", i, got)
		}
	}
}

// A closed peer surfaces as a read error, which is how the hub notices a
// disconnect and releases the session reference.
func TestTCPReadFailsAfterThePeerCloses(t *testing.T) {
	client, server := tcpPair(t)
	if err := client.Close(CloseNormal, "bye"); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); err == nil {
		t.Fatal("expected a read error once the peer closed")
	} else if ctx.Err() != nil {
		t.Fatalf("read timed out instead of seeing the close: %v", err)
	}
}

// The hub fans out to a member from its write loop while other goroutines may
// also write; interleaved bytes would corrupt the NDJSON stream, so Write holds
// a mutex. Run under -race to catch the data race too.
func TestTCPConcurrentWritesDoNotInterleave(t *testing.T) {
	client, server := tcpPair(t)

	const writers, each = 8, 25
	var wg sync.WaitGroup
	for w := 0; w < writers; w++ {
		wg.Add(1)
		go func(w int) {
			defer wg.Done()
			msg := []byte(`{"w":` + strings.Repeat("0", 200) + string(rune('a'+w)) + `}`)
			for i := 0; i < each; i++ {
				if err := client.Write(context.Background(), msg); err != nil {
					return
				}
			}
		}(w)
	}

	read := make(chan string, writers*each)
	go func() {
		for i := 0; i < writers*each; i++ {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			data, err := server.Read(ctx)
			cancel()
			if err != nil {
				close(read)
				return
			}
			read <- string(data)
		}
		close(read)
	}()

	wg.Wait()
	n := 0
	for got := range read {
		n++
		if !strings.HasPrefix(got, `{"w":`) || !strings.HasSuffix(got, `}`) || strings.Contains(got, "\n") {
			t.Fatalf("interleaved frame: %q", got)
		}
	}
	if n != writers*each {
		t.Errorf("read %d frames, want %d", n, writers*each)
	}
}

func TestTCPRemoteAddrIdentifiesThePeer(t *testing.T) {
	_, server := tcpPair(t)
	if addr := server.RemoteAddr(); !strings.HasPrefix(addr, "127.0.0.1:") {
		t.Errorf("RemoteAddr() = %q, want a 127.0.0.1 host:port", addr)
	}
}

// The TCP adapter has no close codes to send; Close must still shut the socket
// (and the hub calls it with codes the WS adapter maps).
func TestTCPCloseIgnoresTheCodeAndShutsTheSocket(t *testing.T) {
	client, server := tcpPair(t)
	if err := server.Close(ClosePolicyViolation, "expected hello"); err != nil {
		t.Fatalf("close: %v", err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if _, err := client.Read(ctx); err == nil {
		t.Fatal("expected the peer's read to fail after Close")
	}
}

func TestDialTCPFailsOnAClosedPort(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := ln.Addr().String()
	ln.Close() // nothing is listening now
	if c, err := dialTCP(addr); err == nil {
		c.Close(CloseNormal, "")
		t.Fatal("expected dialTCP to fail against a closed port")
	}
}

// ----- WebSocket adapter -----

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

func TestDialWSFailsAgainstANonWebSocketEndpoint(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK) // a plain 200, no upgrade
	}))
	defer srv.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if c, err := dialWS(ctx, "ws"+strings.TrimPrefix(srv.URL, "http")); err == nil {
		c.Close(CloseNormal, "")
		t.Fatal("expected dialWS to fail without an upgrade")
	}
}

// Origin checking is deliberately off (InsecureSkipVerify): a Stencil client is
// authenticated by the in-band hello token, not by the browser origin, so an
// extension page or a file:// document may connect and simply fails auth without
// a valid token. A cross-origin handshake must therefore upgrade cleanly.
//
// dialWS is no use here — the Go dialer sends no Origin header at all — so this
// drives the library directly to put a foreign Origin on the wire.
func TestAcceptWSUpgradesFromAForeignOrigin(t *testing.T) {
	accepted := make(chan error, 1)
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		c, err := AcceptWS(w, r)
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

// A WS peer that goes silent (never reads, so never pongs) must be reaped
// within the keepalive window — WS conns are hijacked, so nothing else bounds
// a half-open peer. The production cadence is generous; the vars exist so the
// test can shorten it (set BEFORE the pair is created — the pinger reads once).
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

// An active peer must NOT be reaped: while both ends keep a Read pending (as
// the hub does), pings are answered and pongs consumed, and traffic still
// flows after several keepalive rounds have elapsed.
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

// Close codes are a transport-neutral set the WS adapter maps to RFC6455 status
// codes; they must stay inside the range that mapping is valid for.
func TestCloseCodesAreValidRFC6455StatusCodes(t *testing.T) {
	for name, code := range map[string]int{
		"CloseNormal": CloseNormal, "ClosePolicyViolation": ClosePolicyViolation,
	} {
		if code < 1000 || code > 4999 {
			t.Errorf("%s = %d, outside the RFC6455 status range", name, code)
		}
	}
	if CloseNormal != 1000 || ClosePolicyViolation != 1008 {
		t.Error("close codes drifted from the RFC6455 values clients expect")
	}
}
