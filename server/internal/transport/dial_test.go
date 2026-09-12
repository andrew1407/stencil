package transport

import (
	"context"
	"net"

	"github.com/coder/websocket"
)

// The client halves these tests need. The package exports no dialer — nothing
// in the server dials itself — so they are built here from the same adapters
// the accept side uses. internal/testutil carries the twin for other packages.
func dialTCP(addr string) (Conn, error) {
	c, err := net.Dial("tcp", addr)
	if err != nil {
		return nil, err
	}
	return NewTCP(c), nil
}

func dialWS(ctx context.Context, url string) (Conn, error) {
	c, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	c.SetReadLimit(MaxMessageBytes)
	return &wsConn{c: c, remote: url}, nil
}
