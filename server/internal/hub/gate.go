package hub

// What a connection must keep passing after its hello: a project hello names a real project, and the
// token it authenticated with stays valid for as long as the connection lives.

import (
	"context"
	"errors"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
	"stencil/server/internal/validate"
)

// errNoProject ends a hello that names no existing project.
var errNoProject = errors.New("hub: hello names no existing project")

// tokenExpiredNotice is the last frame a connection gets when its session's token expires.
var tokenExpiredNotice = protocol.WSMessage{
	Type:    protocol.WSError,
	Code:    protocol.CodeUnauthorized,
	Message: "token expired; reconnect with a fresh token",
}

// checkProject refuses a hello whose projectId is not the id of an existing project, before a session
// and its bus subscription are spent on it. It writes the refusal and closes the connection itself.
func (h *Hub) checkProject(ctx context.Context, conn transport.Conn, id string) error {
	if !validate.ProjectID(id) {
		refuseHello(ctx, conn, protocol.CodeNotFound, "project not found")
		return errNoProject
	}
	octx, cancel := context.WithTimeout(ctx, h.tune.OpTimeout)
	defer cancel()
	ok, err := h.store.ProjectExists(octx, id)
	switch {
	case err != nil:
		refuseHello(ctx, conn, protocol.CodeInternal, "could not load project")
		return err
	case !ok:
		refuseHello(ctx, conn, protocol.CodeNotFound, "project not found")
		return errNoProject
	}
	return nil
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
