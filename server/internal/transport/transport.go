// Package transport abstracts the live-edit connection so the hub is agnostic
// to how bytes arrive. A Conn carries one protocol.WSMessage (compact JSON) per
// Read/Write. Two implementations share the hub:
//
//   - WebSocket (ws.go): each message is one text frame. Used by the browser
//     (native WebSocket API) and the extension.
//   - TCP (tcp.go): each message is one newline-delimited JSON record (NDJSON).
//     Used by the desktop (QTcpSocket) and the Zig CLI (std.net) so neither needs
//     a third-party WebSocket library nor a hand-rolled RFC6455 framer.
//
// Both deliver identical protocol messages, so a desktop, browser, CLI and
// extension client editing one project all land in the same hub session.
package transport

import "context"

// MaxMessageBytes caps a single inbound message on either transport, bounding
// memory against a hostile or buggy peer. Large enough for a base64 image
// payload in an edit/save.
const MaxMessageBytes = 16 << 20

// Close codes (a small transport-neutral set; the WS adapter maps these to
// RFC6455 status codes, the TCP adapter ignores them).
const (
	CloseNormal          = 1000
	ClosePolicyViolation = 1008
	CloseInternal        = 1011
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
