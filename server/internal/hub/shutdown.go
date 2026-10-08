package hub

// Live edits are relayed, never persisted (only `save` commits), so a restart
// mid-session drops everything since the last save. The server used to just
// close the sockets; it now says so first, and clients can surface it.

import (
	"context"
	"encoding/json"
	"sync"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// shutdownNotice is the last frame a live editor gets.
var shutdownNotice = protocol.WSMessage{
	Type:    protocol.WSError,
	Code:    protocol.CodeShutdown,
	Message: "server is shutting down; unsaved live edits are lost — save and reconnect",
}

// CloseAll tells every live connection the server is going away (unsaved live edits die with it) and
// cancels its context so the handler unwinds and releases the conn. Pair it with Close, after the drain.
func (h *Hub) CloseAll() {
	h.closeAll(h.liveConns())
}

// Close ends the hub's own context, so it comes last: the goodbye writes, the final peer-leave publish
// and an in-flight save all ride it, and cancelling it earlier is what silences the notice.
func (h *Hub) Close() {
	h.cancel()
}

// Drain waits, until ctx ends, for every session's run loop to return; a run loop returns once its last
// member has left and every save its worker held has committed and been announced. Call it after CloseAll.
func (h *Hub) Drain(ctx context.Context) error {
	for {
		h.mu.Lock()
		var next *session
		for s := range h.loops {
			next = s
			break
		}
		h.mu.Unlock()
		if next == nil {
			return nil
		}
		select {
		case <-next.exited:
		case <-ctx.Done():
			return ctx.Err()
		}
	}
}

// liveConns snapshots the tracked connections so the notice + cancel run outside
// the hub lock (a write must never block connects and disconnects).
func (h *Hub) liveConns() []*connReg {
	h.mu.Lock()
	defer h.mu.Unlock()
	out := make([]*connReg, 0, len(h.conns))
	for reg := range h.conns {
		out = append(out, reg)
	}
	return out
}

// closeAll notices then cancels every tracked connection in parallel: the notices are best-effort writes,
// and one wedged peer must not spend another peer's share of the shutdown budget.
func (h *Hub) closeAll(regs []*connReg) {
	var wg sync.WaitGroup
	for _, reg := range regs {
		wg.Add(1)
		go func(reg *connReg) {
			defer wg.Done()
			h.notify(reg.conn, shutdownNotice)
			reg.cancel()
		}(reg)
	}
	wg.Wait()
}

// notify best-effort tells one peer why it is about to be closed, on a deadline of its own: the
// connection's context is the thing about to be cancelled.
func (h *Hub) notify(conn transport.Conn, msg protocol.WSMessage) {
	if conn == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), h.tune.NoticeTimeout)
	defer cancel()
	writeMsg(ctx, conn, msg)
}

// writeMsg marshals and sends a single message (best effort).
func writeMsg(ctx context.Context, conn transport.Conn, msg protocol.WSMessage) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	_ = conn.Write(ctx, data)
}
