package hub

import (
	"context"
	"encoding/json"
	"net"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

const goodToken = "good-token"

// newTestHub wires a hub onto an in-memory session store, seeded so goodToken resolves.
func newTestHub(t *testing.T, opts ...Option) *Hub {
	t.Helper()
	st := testutil.NewMemStore()
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	h := New(context.Background(), eventbus.NewInProc(), st, opts...)
	t.Cleanup(h.Close) // the hub holds a context of its own, so a test must end it
	return h
}

// startTCP runs a hub TCP listener and returns its address.
func startTCP(t *testing.T, h *Hub) string {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go h.ServeListener(ln)
	return ln.Addr().String()
}

func send(t *testing.T, c transport.Conn, msg protocol.WSMessage) {
	t.Helper()
	data, _ := json.Marshal(msg)
	if err := c.Write(context.Background(), data); err != nil {
		t.Fatalf("write: %v", err)
	}
}

// readUntil reads frames until one of the wanted type arrives or it times out.
func readUntil(t *testing.T, c transport.Conn, want string) protocol.WSMessage {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for {
		ctx, cancel := context.WithDeadline(context.Background(), deadline)
		raw, err := c.Read(ctx)
		cancel()
		if err != nil {
			t.Fatalf("read waiting for %q: %v", want, err)
		}
		var m protocol.WSMessage
		if json.Unmarshal(raw, &m) != nil {
			continue
		}
		if m.Type == want {
			return m
		}
	}
}

// readEvent reads project events until the one about id arrives.
func readEvent(t *testing.T, c transport.Conn, id string) protocol.WSMessage {
	t.Helper()
	for {
		if m := readUntil(t, c, protocol.WSProjectEv); m.Project != nil && m.Project.ID == id {
			return m
		}
	}
}

// expectClosed asserts the peer hung up within the deadline. Frames already in flight are drained: only
// a read error that is NOT our own deadline proves the server closed the connection.
func expectClosed(t *testing.T, c transport.Conn, what string) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	for {
		raw, err := c.Read(ctx)
		if err != nil {
			if ctx.Err() != nil {
				t.Fatalf("%s: connection still open after 3s", what)
			}
			return // the peer hung up, which is what we wanted
		}
		t.Logf("%s: drained a frame that raced the close: %s", what, raw)
	}
}

// joinFeed connects over TCP, says hello, and waits until the feed delivers: a probe event is published
// until the connection reads one, since the hello has no acknowledgement.
func joinFeed(t *testing.T, h *Hub, addr string) transport.Conn {
	t.Helper()
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken})
	awaitFeed(t, h, c)
	return c
}

// awaitFeed publishes a probe every few ms under one long read: a WebSocket read whose context is
// cancelled closes the connection, so the read is never cut short to retry.
func awaitFeed(t *testing.T, h *Hub, c transport.Conn) {
	t.Helper()
	done := make(chan struct{})
	defer close(done)
	go func() {
		for {
			eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_probe"})
			select {
			case <-done:
				return
			case <-time.After(10 * time.Millisecond):
			}
		}
	}()
	readEvent(t, c, "p_probe")
}

// waitFor polls cond up to ~2s so tests don't race the hub's connect/disconnect goroutines.
func waitFor(t *testing.T, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if cond() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("condition not met within timeout")
}

func liveConnCount(h *Hub) int {
	h.mu.Lock()
	defer h.mu.Unlock()
	return len(h.conns)
}

// The feed carries every project event to a TCP peer, metadata only.
func TestFeedDeliversProjectEventsOverTCP(t *testing.T) {
	h := newTestHub(t)
	c := joinFeed(t, h, startTCP(t, h))
	rec := protocol.ProjectRecord{ID: "p_t_a", Name: "P", Version: 3, Layout: json.RawMessage(`{"lines":[1]}`)}
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventCreated, rec)
	got := readEvent(t, c, "p_t_a")
	if got.Event != protocol.EventCreated || got.Project.Version != 3 || got.Project.Layout != nil {
		t.Fatalf("feed got %+v, want a created event with metadata only", got)
	}
}

// The same feed over the WebSocket adapter.
func TestFeedDeliversProjectEventsOverWebSocket(t *testing.T) {
	h := newTestHub(t)
	srv := httptest.NewServer(h.WSHandler())
	t.Cleanup(srv.Close)
	c, err := testutil.DialWS(context.Background(), "ws"+strings.TrimPrefix(srv.URL, "http"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ClientID: "A"})
	awaitFeed(t, h, c)
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventDeleted, protocol.ProjectRecord{ID: "p_t_b"})
	if got := readEvent(t, c, "p_t_b"); got.Event != protocol.EventDeleted {
		t.Fatalf("ws feed got %+v", got)
	}
}

// On shutdown CloseAll cancels every live connection's context: a blocked TCP reader is interrupted and
// the connection is forgotten. Without ctx-aware TCP reads this would hang.
func TestCloseAllDrainsConnections(t *testing.T) {
	h := newTestHub(t)
	c := joinFeed(t, h, startTCP(t, h))
	waitFor(t, func() bool { return liveConnCount(h) == 1 })
	h.CloseAll()
	expectClosed(t, c, "CloseAll")
	waitFor(t, func() bool { return liveConnCount(h) == 0 })
}
