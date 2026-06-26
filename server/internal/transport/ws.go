package transport

import (
	"context"
	"net/http"

	"github.com/coder/websocket"
)

// wsConn adapts a coder/websocket connection to Conn.
type wsConn struct {
	c      *websocket.Conn
	remote string
}

// AcceptWS upgrades an HTTP request to a WebSocket Conn. Origin checking is
// disabled because authentication is performed by the in-band hello frame
// (a bearer token), not by the browser origin — any origin may attempt to
// connect but cannot join a session without a valid token.
func AcceptWS(w http.ResponseWriter, r *http.Request) (Conn, error) {
	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		return nil, err
	}
	c.SetReadLimit(MaxMessageBytes)
	return &wsConn{c: c, remote: r.RemoteAddr}, nil
}

// DialWS connects to a WebSocket server (used by tests and Go-side clients).
func DialWS(ctx context.Context, url string) (Conn, error) {
	c, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		return nil, err
	}
	c.SetReadLimit(MaxMessageBytes)
	return &wsConn{c: c, remote: url}, nil
}

func (x *wsConn) Read(ctx context.Context) ([]byte, error) {
	_, data, err := x.c.Read(ctx)
	return data, err
}

func (x *wsConn) Write(ctx context.Context, data []byte) error {
	return x.c.Write(ctx, websocket.MessageText, data)
}

func (x *wsConn) Close(code int, reason string) error {
	return x.c.Close(websocket.StatusCode(code), reason)
}

func (x *wsConn) RemoteAddr() string { return x.remote }
