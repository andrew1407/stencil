package transport

import (
	"bufio"
	"context"
	"io"
	"net"
	"sync"
	"time"
)

// tcpConn adapts a stream net.Conn to Conn using newline-delimited JSON: compact JSON never contains a
// literal newline, so '\n' is an unambiguous frame delimiter any client can produce and parse.
type tcpConn struct {
	conn net.Conn
	sc   *bufio.Scanner
	wmu  sync.Mutex
}

// tcpIdleTimeout bounds how long a Read with no per-call deadline may block with no bytes before the peer
// is reaped. A var so tests can shorten it; generous, so a live but idle co-editor is not dropped.
var tcpIdleTimeout = 5 * time.Minute

// NewTCP wraps an accepted/ dialed net.Conn as a Conn.
func NewTCP(conn net.Conn) Conn {
	sc := bufio.NewScanner(conn)
	// +1 for the delimiter: the scanner buffers the '\n' too, so a cap of exactly
	// MaxMessageBytes would reject a message OF that size, which WS accepts.
	sc.Buffer(make([]byte, 0, 64*1024), MaxMessageBytes+1)
	return &tcpConn{conn: conn, sc: sc}
}

func (t *tcpConn) Read(ctx context.Context) ([]byte, error) {
	// An already-cancelled context wins before any bytes are consumed; without this, a Read on a torn-down
	// connection still delivers whatever the scanner had buffered.
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	// Deadline first, then ctx cancellation: Scan() blocks in conn.Read, so cancellation is delivered by
	// shoving the read deadline into the past. AfterFunc always sets the later value (now), so it wins.
	if dl, ok := ctx.Deadline(); ok {
		_ = t.conn.SetReadDeadline(dl)
	} else {
		_ = t.conn.SetReadDeadline(time.Now().Add(tcpIdleTimeout))
	}
	stop := context.AfterFunc(ctx, func() {
		_ = t.conn.SetReadDeadline(time.Now())
	})
	defer stop() // release the hook (and its timer) so it never leaks per Read
	if !t.sc.Scan() {
		// A cancelled/expired ctx takes precedence so callers see context errors
		// rather than the timeout the deadline poke produced.
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		if err := t.sc.Err(); err != nil {
			return nil, err
		}
		return nil, io.EOF
	}
	// Scanner reuses its buffer; copy before returning.
	b := t.sc.Bytes()
	out := make([]byte, len(b))
	copy(out, b)
	return out, nil
}

func (t *tcpConn) Write(ctx context.Context, data []byte) error {
	t.wmu.Lock()
	defer t.wmu.Unlock()
	if dl, ok := ctx.Deadline(); ok {
		_ = t.conn.SetWriteDeadline(dl)
	} else {
		_ = t.conn.SetWriteDeadline(time.Time{})
	}
	if _, err := t.conn.Write(data); err != nil {
		return err
	}
	_, err := t.conn.Write([]byte{'\n'})
	return err
}

func (t *tcpConn) Close(_ int, _ string) error { return t.conn.Close() }

func (t *tcpConn) RemoteAddr() string { return t.conn.RemoteAddr().String() }
