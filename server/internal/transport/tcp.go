package transport

import (
	"bufio"
	"context"
	"io"
	"net"
	"sync"
	"time"
)

// tcpConn adapts a stream net.Conn to Conn using newline-delimited JSON. Compact
// JSON never contains a literal newline, so '\n' is an unambiguous frame
// delimiter that any client (Zig std.net, Qt QTcpSocket, Go) can produce and
// parse trivially.
type tcpConn struct {
	conn net.Conn
	sc   *bufio.Scanner
	wmu  sync.Mutex
}

// tcpIdleTimeout bounds how long a Read with no per-call deadline may block with
// no bytes arriving before the peer is treated as dead and reaped. It is a var
// (not const) so tests can shorten it; the value is generous so a live but idle
// editor (still receiving cursor/presence/ping traffic while co-editing) is not
// dropped, while a wedged or vanished TCP peer is eventually torn down.
var tcpIdleTimeout = 5 * time.Minute

// NewTCP wraps an accepted/ dialed net.Conn as a Conn.
func NewTCP(conn net.Conn) Conn {
	sc := bufio.NewScanner(conn)
	sc.Buffer(make([]byte, 0, 64*1024), MaxMessageBytes)
	return &tcpConn{conn: conn, sc: sc}
}

// DialTCP connects to a TCP edit endpoint (used by tests and Go-side clients).
func DialTCP(addr string) (Conn, error) {
	c, err := net.Dial("tcp", addr)
	if err != nil {
		return nil, err
	}
	return NewTCP(c), nil
}

func (t *tcpConn) Read(ctx context.Context) ([]byte, error) {
	// Set the effective deadline first, then arm ctx cancellation. bufio.Scanner
	// has no context awareness and Scan() blocks in conn.Read, so cancellation is
	// delivered by shoving the read deadline into the past, which makes the
	// blocked Scan return immediately. AfterFunc always sets the *later* value
	// (now), so it wins over the line below even under a start-time race.
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
