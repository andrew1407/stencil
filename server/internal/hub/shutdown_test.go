package hub

import (
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// Live edits are relayed, never persisted, so a restart mid-session loses
// everything since the last save. Shutdown must say so before hanging up —
// silence is indistinguishable from a network blip.
func TestShutdownNotifiesLiveConnections(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	editor := joinProject(t, addr, "p_t_a", "A")

	// The global /events feed is a connection too, and it also gets the notice.
	feed, err := transport.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { feed.Close(0, "") })
	send(t, feed, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken})
	// Both handlers must be tracked before shutting down: a conn the hub has not
	// registered yet is one it cannot notify (and the test would race the accept).
	waitFor(t, func() bool {
		h.mu.Lock()
		defer h.mu.Unlock()
		return len(h.conns) == 2
	})

	h.CloseAll()

	for name, c := range map[string]transport.Conn{"editor": editor, "events feed": feed} {
		got := readUntil(t, c, protocol.WSError)
		if got.Code != protocol.CodeShutdown {
			t.Errorf("%s: code %q, want %q", name, got.Code, protocol.CodeShutdown)
		}
		if got.Message == "" {
			t.Errorf("%s: the notice should say what was lost", name)
		}
		expectClosed(t, c, name+" after shutdown")
	}
}
