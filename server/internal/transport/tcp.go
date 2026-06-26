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
	if dl, ok := ctx.Deadline(); ok {
		_ = t.conn.SetReadDeadline(dl)
	} else {
		_ = t.conn.SetReadDeadline(time.Time{})
	}
	if !t.sc.Scan() {
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
