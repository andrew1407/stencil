package hub

import (
	"context"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

// Live edits are relayed, never persisted, so a restart mid-session loses everything since the last save.
// Shutdown must say so before hanging up — silence is indistinguishable from a network blip.
func TestShutdownNotifiesLiveConnections(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	editor := joinProject(t, addr, "p_t_a", "A")

	// The global /events feed is a connection too, and it also gets the notice.
	feed, err := testutil.DialTCP(addr)
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

// The hub's context is not the signal context: cancelling that must not cut the goodbye write, an
// in-flight save (persist.go) or the drain's last publish (relay.go) short. Close is what ends it.
func TestHubContextOutlivesTheSignalContext(t *testing.T) {
	signalCtx, signalled := context.WithCancel(context.Background())
	h := New(signalCtx, nil, eventbus.NewInProc(), nil)

	signalled()
	if err := h.ctx.Err(); err != nil {
		t.Fatalf("the hub died with the signal: %v", err)
	}

	h.Close()
	if h.ctx.Err() == nil {
		t.Error("Close must end the hub's context")
	}
}
