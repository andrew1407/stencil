package hub

import (
	"errors"
	"net"
	"sync"
	"syscall"
	"testing"
	"time"
)

// flakyListener fails its first accepts with a resource error, then hands out one real connection, then
// reports itself closed.
type flakyListener struct {
	mu    sync.Mutex
	fails int
	conn  net.Conn
	calls int
}

func (l *flakyListener) Accept() (net.Conn, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.calls++
	switch {
	case l.fails > 0:
		l.fails--
		return nil, &net.OpError{Op: "accept", Net: "tcp", Err: syscall.EMFILE}
	case l.conn != nil:
		c := l.conn
		l.conn = nil
		return c, nil
	default:
		return nil, net.ErrClosed
	}
}

func (l *flakyListener) Close() error   { return nil }
func (l *flakyListener) Addr() net.Addr { return &net.TCPAddr{} }

// A transient accept error is retried, not fatal: the listener keeps serving and returns only once closed.
func TestServeListenerRetriesATransientAcceptError(t *testing.T) {
	h := newTestHub(t)
	server, client := net.Pipe()
	client.Close() // the served connection ends at once, so the drain does not wait out a hello
	ln := &flakyListener{fails: 3, conn: server}
	done := make(chan error, 1)
	go func() { done <- h.ServeListener(ln) }()
	select {
	case err := <-done:
		if !errors.Is(err, net.ErrClosed) {
			t.Fatalf("ServeListener returned %v, want net.ErrClosed", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("ServeListener never returned")
	}
	if ln.calls != 5 {
		t.Fatalf("accept ran %d times, want 3 failures, the conn and the close", ln.calls)
	}
}

// The backoff never waits past its cap, and a closing hub ends the wait.
func TestServeListenerStopsRetryingWhenTheHubCloses(t *testing.T) {
	h := newTestHub(t)
	ln := &flakyListener{fails: 1 << 30}
	done := make(chan error, 1)
	go func() { done <- h.ServeListener(ln) }()
	time.Sleep(20 * time.Millisecond)
	h.Close()
	select {
	case err := <-done:
		if err == nil || errors.Is(err, net.ErrClosed) {
			t.Fatalf("ServeListener returned %v, want the accept error", err)
		}
	case <-time.After(2 * acceptBackoffMax):
		t.Fatal("a closed hub kept retrying")
	}
}
