package transport

import (
	"bufio"
	"context"
	"errors"
	"io"
	"net"
	"sync/atomic"
	"time"
)

// tcpConn adapts a stream net.Conn to Conn using newline-delimited JSON: compact JSON never contains a
// literal newline, so '\n' is an unambiguous frame delimiter any client can produce and parse.
type tcpConn struct {
	conn    net.Conn
	rd      *bufio.Reader
	limit   atomic.Int64  // the longest frame Read accepts, without its delimiter
	wsem    chan struct{} // one slot: the writer's lock, taken under the caller's ctx
	scratch []byte
}

// A Read with no deadline of its own gives up after tcpIdleTimeout (generous: an idle feed stays), a
// frame's Write after tcpWriteTimeout (a peer that stops reading cannot pin its writer). Set by Configure.
var (
	tcpIdleTimeout  = 5 * time.Minute
	tcpWriteTimeout = 30 * time.Second
)

// frameEnd is the NDJSON delimiter, written with the frame in one call.
var frameEnd = []byte{'\n'}

// scratchKeep bounds the joined-frame buffer a TLS conn keeps between writes; a larger frame's is dropped.
const scratchKeep = 64 << 10

// NewTCP wraps an accepted or dialed net.Conn as a Conn, reading under MaxHelloBytes until SetReadLimit.
func NewTCP(conn net.Conn) Conn {
	t := &tcpConn{conn: conn, rd: bufio.NewReaderSize(conn, scratchKeep), wsem: make(chan struct{}, 1)}
	t.limit.Store(MaxHelloBytes)
	return t
}

func (t *tcpConn) SetReadLimit(n int64) { t.limit.Store(n) }

func (t *tcpConn) Read(ctx context.Context) ([]byte, error) {
	// An already-cancelled context wins before any bytes are consumed; without this, a Read on a torn-down
	// connection still delivers whatever the reader had buffered.
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	// Deadline first, then ctx cancellation: the reader blocks in conn.Read, so cancellation is delivered by
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
	frame, err := t.readFrame()
	if err != nil {
		// A cancelled/expired ctx takes precedence so callers see context errors
		// rather than the timeout the deadline poke produced.
		if cerr := ctx.Err(); cerr != nil {
			return nil, cerr
		}
		return nil, err
	}
	return frame, nil
}

// readFrame collects one line into a fresh slice, refusing it as soon as it outgrows the read limit; a
// final unterminated line at EOF still counts as a frame.
func (t *tcpConn) readFrame() ([]byte, error) {
	limit := int(t.limit.Load())
	var out []byte
	for {
		part, err := t.rd.ReadSlice('\n')
		if len(out)+len(part) > limit+1 {
			return nil, ErrFrameTooLarge
		}
		out = append(out, part...)
		switch {
		case err == nil:
			return out[:len(out)-1], nil
		case errors.Is(err, bufio.ErrBufferFull):
			continue
		case errors.Is(err, io.EOF) && len(out) > 0:
			return out, nil
		default:
			return nil, err
		}
	}
}

// Write sends data and its delimiter in one call, under the earlier of ctx's deadline and tcpWriteTimeout:
// writev on a plain socket, one joined buffer elsewhere, since a TLS conn seals each Write as its own record.
// A write still waiting for the lock gives up when ctx ends, so a notice never queues behind a stuck frame.
func (t *tcpConn) Write(ctx context.Context, data []byte) error {
	select {
	case t.wsem <- struct{}{}:
	case <-ctx.Done():
		return ctx.Err()
	}
	defer func() { <-t.wsem }()
	dl := time.Now().Add(tcpWriteTimeout)
	if d, ok := ctx.Deadline(); ok && d.Before(dl) {
		dl = d
	}
	_ = t.conn.SetWriteDeadline(dl)
	if _, plain := t.conn.(*net.TCPConn); plain {
		bufs := net.Buffers{data, frameEnd}
		_, err := bufs.WriteTo(t.conn)
		return err
	}
	frame := append(append(t.scratch[:0], data...), frameEnd...)
	_, err := t.conn.Write(frame)
	if cap(frame) <= scratchKeep {
		t.scratch = frame
	}
	return err
}

func (t *tcpConn) Close(_ int, _ string) error { return t.conn.Close() }

func (t *tcpConn) RemoteAddr() string { return t.conn.RemoteAddr().String() }
