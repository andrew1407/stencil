package hub

// What a connection must keep passing after its hello: it names no project, and the token it
// authenticated with stays valid for as long as the connection lives.

import (
	"context"
	"errors"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// errProjectHello ends a hello that names a project.
var errProjectHello = errors.New("hub: hello names a project; no session is served")

// tokenExpiredNotice is the last frame a connection gets when its session's token expires.
var tokenExpiredNotice = protocol.WSMessage{
	Type:    protocol.WSError,
	Code:    protocol.CodeUnauthorized,
	Message: "token expired; reconnect with a fresh token",
}

// expireAt hangs up the connection when its token's session expires (epoch ms; 0 = never), telling the
// peer why first, as a shutdown does. The returned func disarms it once the connection ends on its own.
func (h *Hub) expireAt(expiresAtMs int64, conn transport.Conn, hangUp context.CancelFunc) func() {
	if expiresAtMs == 0 {
		return func() {}
	}
	t := time.AfterFunc(time.Until(time.UnixMilli(expiresAtMs)), func() {
		h.notify(conn, tokenExpiredNotice)
		hangUp()
	})
	return func() { t.Stop() }
}
