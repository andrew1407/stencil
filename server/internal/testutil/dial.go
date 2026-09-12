package testutil

import (
	"context"
	"net"

	"github.com/coder/websocket"

	"stencil/server/internal/transport"
)

// DialTCP opens an NDJSON edit connection to addr. It and DialWS are the Go
// client halves of the live-edit transports; nothing in the server dials itself
// (the real clients are JS, C++ and Zig), so they live here, not in transport.
func DialTCP(addr string) (transport.Conn, error) {
	c, err := net.Dial("tcp", addr)
	if err != nil {
		return nil, err
	}
	return transport.NewTCP(c), nil
}

func DialWS(ctx context.Context, url string) (transport.Conn, error) {
	c, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	c.SetReadLimit(transport.MaxMessageBytes)
	return &wsClient{c: c, remote: url}, nil
}

// wsClient is the client twin of transport/ws.go's accepted-side adapter: same
// framing and read limit, minus the server's keepalive pinger.
type wsClient struct {
	c      *websocket.Conn
	remote string
}

func (w *wsClient) Read(ctx context.Context) ([]byte, error) {
	_, data, err := w.c.Read(ctx)
	return data, err
}

func (w *wsClient) Write(ctx context.Context, data []byte) error {
	return w.c.Write(ctx, websocket.MessageText, data)
}

func (w *wsClient) Close(code int, reason string) error {
	return w.c.Close(websocket.StatusCode(code), reason)
}

func (w *wsClient) RemoteAddr() string { return w.remote }
