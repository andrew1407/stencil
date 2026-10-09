package hub

// The hello handshake and the guards around it: a connection that skips hello, stalls, oversteps the
// hello cap or exceeds its address's share of connections is refused without touching the others.

import (
	"context"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

func TestUnauthorizedRejected(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: "bad"})
	if got := readUntil(t, c, protocol.WSError); got.Code != protocol.CodeUnauthorized {
		t.Fatalf("expected unauthorized, got %q", got.Code)
	}
}

func TestHelloRequiredFirst(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, _ := testutil.DialTCP(addr)
	defer c.Close(0, "")
	send(t, c, protocol.WSMessage{Type: protocol.WSError}) // not a hello: the server must hang up
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected connection close after non-hello first frame")
	}
}

// A connection that never sends its hello is closed once HelloTimeout elapses.
func TestHelloTimeoutClosesSilentPeer(t *testing.T) {
	h := newTestHub(t, WithTuning(Tuning{HelloTimeout: 150 * time.Millisecond}))
	addr := startTCP(t, h)
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected the connection to be closed after the hello timeout")
	}
}

// A non-JSON frame on the feed is ignored, not fatal: the feed keeps delivering to that peer.
func TestMalformedFrameDoesNotDropFeed(t *testing.T) {
	h := newTestHub(t)
	c := joinFeed(t, h, startTCP(t, h))
	if err := c.Write(context.Background(), []byte("not json at all {{{")); err != nil {
		t.Fatalf("write garbage: %v", err)
	}
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_t_a"})
	readEvent(t, c, "p_t_a")
}

// A first frame past MaxHelloBytes is refused: the connection closes before a token was looked up.
func TestOversizedHelloRefused(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	padding := strings.Repeat("a", transport.MaxHelloBytes)
	_ = c.Write(context.Background(), []byte(`{"type":"hello","token":"`+goodToken+`","name":"`+padding+`"}`))
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected the connection to close on a hello past the hello cap")
	}
}

// After the hello a feed connection still reads under the hello cap: a frame within it is discarded and the
// feed goes on, one past it ends the connection.
func TestFeedKeepsTheHelloCapAfterTheHello(t *testing.T) {
	h := newTestHub(t)
	c := joinFeed(t, h, startTCP(t, h))
	if err := c.Write(context.Background(), []byte(strings.Repeat("x", transport.MaxHelloBytes))); err != nil {
		t.Fatalf("write: %v", err)
	}
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_t_a"})
	readEvent(t, c, "p_t_a")
	if err := c.Write(context.Background(), []byte(strings.Repeat("x", transport.MaxHelloBytes+1024))); err != nil {
		t.Fatalf("write: %v", err)
	}
	expectClosed(t, c, "a frame past the hello cap")
}

// The per-IP cap refuses the connection past it with rateLimited, before any hello is read, and a closed
// connection gives its slot back.
func TestPerIPConnectionCapRefusesTheExtraAndReleasesOnClose(t *testing.T) {
	h := newTestHub(t, WithMaxConnsPerIP(2))
	addr := startTCP(t, h)
	a := joinFeed(t, h, addr)
	joinFeed(t, h, addr)

	extra, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { extra.Close(0, "") })
	if got := readUntil(t, extra, protocol.WSError); got.Code != protocol.CodeRateLimited {
		t.Fatalf("the third connection got %+v, want %s", got, protocol.CodeRateLimited)
	}
	expectClosed(t, extra, "over the per-IP cap")

	a.Close(0, "bye")
	waitFor(t, func() bool { return liveConnCount(h) == 2 })
	joinFeed(t, h, addr) // the freed slot admits a new connection
}

// A hub built without WithMaxConnsPerIP keeps the default cap; WithMaxConnsPerIP(0) lifts it.
func TestPerIPCapTuning(t *testing.T) {
	if got := newTestHub(t).perIP.cap; got != defaultMaxConnsPerIP {
		t.Fatalf("default cap %d, want %d", got, defaultMaxConnsPerIP)
	}
	if h := newTestHub(t, WithMaxConnsPerIP(0)); !h.perIP.take("ip") || !h.perIP.take("ip") || h.perIP.cap != 0 {
		t.Fatalf("a zero cap should lift it, got %d", h.perIP.cap)
	}
}
