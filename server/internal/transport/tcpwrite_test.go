package transport

import (
	"context"
	"net"
	"testing"
	"time"
)

// A peer that stops reading cannot pin its writer: with no deadline of the caller's, a frame's write
// gives up after tcpWriteTimeout (a net.Pipe has no buffer, so an unread write blocks at once).
func TestTCPWriteGivesUpOnAPeerThatStopsReading(t *testing.T) {
	prev := tcpWriteTimeout
	tcpWriteTimeout = 100 * time.Millisecond
	t.Cleanup(func() { tcpWriteTimeout = prev })
	a, b := net.Pipe()
	t.Cleanup(func() { a.Close(); b.Close() })

	start := time.Now()
	if err := NewTCP(a).Write(context.Background(), []byte(`{"type":"pong"}`)); err == nil {
		t.Fatal("a write nobody reads must fail at its deadline")
	}
	if elapsed := time.Since(start); elapsed > 2*time.Second {
		t.Fatalf("the write took %v, want ~100ms", elapsed)
	}
}

// The frame and its delimiter go out together, and arrive as exactly one NDJSON line.
func TestTCPWriteSendsOneDelimitedFrame(t *testing.T) {
	client, server := tcpPair(t)
	go func() { _ = client.Write(context.Background(), []byte(`{"type":"ping"}`)) }()
	if got := string(read1(t, server)); got != `{"type":"ping"}` {
		t.Fatalf("got %q", got)
	}
}

// Configure sets the timeouts later connections use; a zero field leaves one alone.
func TestConfigureAppliesNonZeroTimeouts(t *testing.T) {
	prev := [4]time.Duration{wsPingInterval, wsPongTimeout, tcpIdleTimeout, tcpWriteTimeout}
	t.Cleanup(func() {
		wsPingInterval, wsPongTimeout, tcpIdleTimeout, tcpWriteTimeout = prev[0], prev[1], prev[2], prev[3]
	})
	Configure(Timeouts{WSPing: 7 * time.Second, TCPWrite: 9 * time.Second})
	if wsPingInterval != 7*time.Second || tcpWriteTimeout != 9*time.Second {
		t.Fatalf("set fields: ping %v write %v", wsPingInterval, tcpWriteTimeout)
	}
	if wsPongTimeout != prev[1] || tcpIdleTimeout != prev[2] {
		t.Fatal("a zero field moved its timeout")
	}
}

// A notice behind a frame stuck on a peer that never reads gives up at its own deadline, not the frame's.
func TestTCPWriteWaitingForTheLockHonoursItsContext(t *testing.T) {
	a, b := net.Pipe()
	t.Cleanup(func() { a.Close(); b.Close() })
	c := NewTCP(a)
	stuck := make(chan struct{})
	go func() {
		defer close(stuck)
		_ = c.Write(context.Background(), []byte(`{"type":"edit"}`)) // holds the lock for tcpWriteTimeout
	}()
	time.Sleep(20 * time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	start := time.Now()
	if err := c.Write(ctx, []byte(`{"type":"error"}`)); err == nil {
		t.Fatal("a notice behind a stuck write must fail at its deadline")
	}
	if elapsed := time.Since(start); elapsed > time.Second {
		t.Fatalf("the notice waited %v behind the stuck frame, want ~100ms", elapsed)
	}
	select {
	case <-stuck:
		t.Fatal("the stuck frame should still be blocked on the silent peer")
	default:
	}
}
