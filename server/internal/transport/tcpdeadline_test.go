package transport

// TCP Read deadlines: the context cancel that unwinds a hijacked reader on
// shutdown, the per-call deadline, and the per-read idle window.

import (
	"context"
	"errors"
	"os"
	"testing"
	"time"
)

// CloseAll cancels each connection's context to unwind a handler blocked in Read. bufio.Scanner has no
// context awareness, so the adapter pokes the read deadline; the caller must still see the ctx error.
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

// A per-call deadline is honoured — what bounds the hub's hello timeout. Either context.DeadlineExceeded
// or the raw os.ErrDeadlineExceeded can surface, so the test accepts both rather than pinning the race.
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

// With no per-call deadline a Read still cannot block forever: a wedged peer is reaped once
// tcpIdleTimeout elapses. The production value is generous, so the test shortens the var.
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
