// Package transport abstracts the live-edit connection so the hub is agnostic to how bytes arrive. A Conn
// carries one protocol.WSMessage (compact JSON) per Read/Write, over two implementations: WebSocket
// (ws.go, one text frame per message — browser and extension) and TCP (tcp.go, one newline-delimited JSON
// record — desktop QTcpSocket and the Zig CLI, so neither needs a WebSocket library). Both deliver
// identical protocol messages, so every client editing one project lands in the same hub session.
package transport

import "context"

// MaxMessageBytes caps a single inbound message on either transport, bounding memory against
// a hostile or buggy peer. Large enough for a base64 image payload in an edit/save.
const MaxMessageBytes = 16 << 20

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
	// Close shuts the connection with a code and human-readable reason.
	Close(code int, reason string) error
	// RemoteAddr identifies the peer for logging.
	RemoteAddr() string
}
