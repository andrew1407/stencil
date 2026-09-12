package transport

// TCP adapter framing: one message per Read/Write, the NDJSON split/reassembly
// the hand-rolled framer owes the Zig CLI and Qt desktop, and the size cap.

import (
	"context"
	"net"
	"strings"
	"sync"
	"testing"
	"time"
)

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
