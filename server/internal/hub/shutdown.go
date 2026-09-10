package hub

// Live edits are relayed, never persisted (only `save` commits), so a restart
// mid-session drops everything since the last save. The server used to just
// close the sockets; it now says so first, and clients can surface it.

import (
	"context"
	"encoding/json"
	"sync"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// shutdownNoticeTimeout bounds the goodbye write, so one wedged peer cannot
// delay the drain.
const shutdownNoticeTimeout = time.Second

// shutdownNotice is the last frame a live editor gets.
var shutdownNotice = protocol.WSMessage{
	Type:    protocol.WSError,
	Code:    protocol.CodeShutdown,
	Message: "server is shutting down; unsaved live edits are lost — save and reconnect",
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

// closeAll notices then cancels every tracked connection, in parallel: the
// notices are best-effort writes, and one wedged peer must not spend another
// peer's share of main's shutdown budget.
func closeAll(regs []*connReg) {
	var wg sync.WaitGroup
	for _, reg := range regs {
		wg.Add(1)
		go func(reg *connReg) {
			defer wg.Done()
			notifyShutdown(reg.conn)
			reg.cancel()
		}(reg)
	}
	wg.Wait()
}

// notifyShutdown best-effort tells one peer why it is about to be closed.
func notifyShutdown(conn transport.Conn) {
	if conn == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), shutdownNoticeTimeout)
	defer cancel()
	writeMsg(ctx, conn, shutdownNotice)
}

// writeMsg marshals and sends a single message (best effort).
func writeMsg(ctx context.Context, conn transport.Conn, msg protocol.WSMessage) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	_ = conn.Write(ctx, data)
}
