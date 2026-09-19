package transport

import (
	"context"
	"net/http"
	"sync"
	"time"

	"github.com/coder/websocket"
)

// Keepalive cadence for accepted connections: a peer that stops answering pings within wsPongTimeout is
// torn down, so a dead peer is detected within interval+timeout (~40s). Vars so tests can shorten them.
var (
	wsPingInterval = 30 * time.Second
	wsPongTimeout  = 10 * time.Second
)

// wsConn adapts a coder/websocket connection to Conn.
type wsConn struct {
	c      *websocket.Conn
	remote string
	done   chan struct{} // stops the keepalive pinger; nil on dialled conns
	once   sync.Once
}

// AcceptWS upgrades an HTTP request to a WebSocket Conn. Origin checking is off because the in-band hello
// frame's bearer token is the auth: any origin may connect, none can join a session without a valid token.
func AcceptWS(rw http.ResponseWriter, req *http.Request) (Conn, error) {
	c, err := websocket.Accept(rw, req, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		return nil, err
	}
	c.SetReadLimit(MaxMessageBytes)
	x := &wsConn{c: c, remote: req.RemoteAddr, done: make(chan struct{})}
	go x.keepalive()
	return x, nil
}

// keepalive pings the peer on a timer; the pong is read by whatever Read the hub has pending. A missed
// pong hard-closes the conn, which unblocks that Read so the half-open peer is reaped.
func (x *wsConn) keepalive() {
	interval, timeout := wsPingInterval, wsPongTimeout // read once: test seams
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-x.done:
			return
		case <-t.C:
			ctx, cancel := context.WithTimeout(context.Background(), timeout)
			err := x.c.Ping(ctx)
			cancel()
			if err != nil {
				x.stop()
				_ = x.c.CloseNow() // dead peer: no close handshake to wait for
				return
			}
		}
	}
}

// stop ends the keepalive pinger (idempotent; no-op on dialled conns).
func (x *wsConn) stop() {
	if x.done != nil {
		x.once.Do(func() { close(x.done) })
	}
}

func (x *wsConn) Read(ctx context.Context) ([]byte, error) {
	_, data, err := x.c.Read(ctx)
	return data, err
}

func (x *wsConn) Write(ctx context.Context, data []byte) error {
	return x.c.Write(ctx, websocket.MessageText, data)
}

func (x *wsConn) Close(code int, reason string) error {
	x.stop()
	return x.c.Close(websocket.StatusCode(code), reason)
}

func (x *wsConn) RemoteAddr() string { return x.remote }
