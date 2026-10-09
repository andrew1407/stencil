// Package transport abstracts the live connection so the hub is agnostic to how bytes arrive. A Conn
// carries one protocol.WSMessage (compact JSON) per Read/Write, over two implementations: WebSocket
// (ws.go, one text frame per message — browser and extension) and TCP (tcp.go, one newline-delimited JSON
// record — desktop QTcpSocket and the Zig CLI, so neither needs a WebSocket library). Both deliver
// identical protocol messages, so every client reaches the same hub feed.
package transport

import (
	"context"
	"errors"
	"time"
)

// MaxHelloBytes caps the first inbound message on either transport: a connection holds no more than this
// until its hello has authenticated, which bounds what a flood of anonymous sockets can pin.
const MaxHelloBytes = 64 << 10

// MaxMessageBytes caps an inbound message once the hello has passed, bounding memory against a hostile
// or buggy peer.
const MaxMessageBytes = 16 << 20

// ErrFrameTooLarge fails a Read whose frame exceeds the connection's read limit.
var ErrFrameTooLarge = errors.New("transport: frame exceeds the read limit")

// Close codes (a small transport-neutral set; the WS adapter maps these to
// RFC6455 status codes, the TCP adapter ignores them).
const (
	CloseNormal          = 1000
	ClosePolicyViolation = 1008
)

// Conn is one bidirectional message stream.
type Conn interface {
	// Read returns the next message, blocking until one arrives, the context is
	// cancelled (where the transport supports it), or the peer closes.
	Read(ctx context.Context) ([]byte, error)
	// Write sends one message.
	Write(ctx context.Context, data []byte) error
	// SetReadLimit caps the next messages Read accepts; a new Conn starts at MaxHelloBytes.
	SetReadLimit(n int64)
	// Close shuts the connection with a code and human-readable reason.
	Close(code int, reason string) error
	// RemoteAddr identifies the peer for logging.
	RemoteAddr() string
}

// Timeouts are the transports' keepalive cadence and deadlines; a zero field keeps the current value.
type Timeouts struct {
	WSPing        time.Duration // keepalive ping cadence on accepted WebSockets
	WSPongTimeout time.Duration // an unanswered ping hard-closes the peer after this
	TCPIdle       time.Duration // a TCP Read with no deadline of its own gives up after this
	TCPWrite      time.Duration // one TCP frame's write deadline
}

// Configure applies t to every connection accepted afterwards; call it once at boot, before a listener.
func Configure(t Timeouts) {
	for _, f := range []struct {
		dst *time.Duration
		v   time.Duration
	}{{&wsPingInterval, t.WSPing}, {&wsPongTimeout, t.WSPongTimeout}, {&tcpIdleTimeout, t.TCPIdle}, {&tcpWriteTimeout, t.TCPWrite}} {
		if f.v > 0 {
			*f.dst = f.v
		}
	}
}
