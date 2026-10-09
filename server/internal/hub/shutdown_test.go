package hub

import (
	"context"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// Shutdown says why before hanging up: silence is indistinguishable from a network blip.
func TestShutdownNotifiesLiveConnections(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	feeds := map[string]transport.Conn{"first feed": joinFeed(t, h, addr), "second feed": joinFeed(t, h, addr)}
	waitFor(t, func() bool { return liveConnCount(h) == 2 })

	h.CloseAll()

	for name, c := range feeds {
		got := readUntil(t, c, protocol.WSError)
		if got.Code != protocol.CodeShutdown {
			t.Errorf("%s: code %q, want %q", name, got.Code, protocol.CodeShutdown)
		}
		if got.Message == "" {
			t.Errorf("%s: the notice should say what to do", name)
		}
		expectClosed(t, c, name+" after shutdown")
	}
}

// The hub's context is not the signal context: cancelling that must not cut the goodbye write short.
// Close is what ends it.
func TestHubContextOutlivesTheSignalContext(t *testing.T) {
	signalCtx, signalled := context.WithCancel(context.Background())
	h := New(signalCtx, eventbus.NewInProc(), nil)

	signalled()
	if err := h.ctx.Err(); err != nil {
		t.Fatalf("the hub died with the signal: %v", err)
	}

	h.Close()
	if h.ctx.Err() == nil {
		t.Error("Close must end the hub's context")
	}
}

// wedgedConn never finishes a write: it holds until the writer's own deadline gives up on it.
type wedgedConn struct{ transport.Conn }

func (wedgedConn) Write(ctx context.Context, _ []byte) error {
	<-ctx.Done()
	return ctx.Err()
}

// A goodbye to a peer that stopped reading is abandoned after Tuning.NoticeTimeout, not the drain.
func TestNoticeTimeoutBoundsAWedgedGoodbye(t *testing.T) {
	if got := newTestHub(t).tune.NoticeTimeout; got != time.Second {
		t.Fatalf("NoticeTimeout default %v, want 1s", got)
	}
	h := newTestHub(t, WithTuning(Tuning{NoticeTimeout: 20 * time.Millisecond}))
	start := time.Now()
	h.closeAll([]*connReg{{cancel: func() {}, conn: wedgedConn{}}})
	if took := time.Since(start); took > 500*time.Millisecond {
		t.Fatalf("a wedged peer held the shutdown for %v past a 20ms notice timeout", took)
	}
}
