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

// newTestHub wires a hub onto the shared in-memory store, seeded with one
// project and one session so goodToken resolves.
func newTestHub(t *testing.T) *Hub {
	t.Helper()
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a", Name: "P", Version: 0})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	h := New(context.Background(), st, eventbus.NewInProc(), st)
	t.Cleanup(h.Close) // the hub holds a context of its own now, so a test must end it
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

// joinProject connects, sends hello + subscribe, and waits for welcome.
func joinProject(t *testing.T, addr, project, client string) transport.Conn {
	t.Helper()
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: project, ClientID: client})
	send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
	readUntil(t, c, protocol.WSWelcome)
	return c
}

// TestConnectionCountTracksLiveMembers verifies the count the REST delete guard reads:
// 0 with no session, then rising and falling as clients join and leave a project.
func TestConnectionCountTracksLiveMembers(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)

	if n := h.ConnectionCount("p_t_a"); n != 0 {
		t.Fatalf("no connections should count 0, got %d", n)
	}
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 2 })

	a.Close(0, "bye")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 1 })
	b.Close(0, "bye")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })
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

// On shutdown CloseAll cancels every live connection's context: a blocked TCP editor's Read is
// interrupted and the session refcount falls to 0. Without ctx-aware TCP reads this would hang.
func TestCloseAllDrainsConnections(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)

	a := joinProject(t, addr, "p_t_a", "A")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 1 })

	h.CloseAll()

	// The server-side read (blocked in Scan) is cancelled and the conn closed, so
	// the client's read returns an error well within the deadline.
	expectClosed(t, a, "CloseAll")
	// The handler unwound and released the session reference.
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })
}

// TestWebSocketTransport exercises the same flow over the WS adapter.
func TestWebSocketTransport(t *testing.T) {
	h := newTestHub(t)
	srv := httptest.NewServer(h.WSHandler())
	t.Cleanup(srv.Close)
	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")

	dial := func(client string) transport.Conn {
		c, err := testutil.DialWS(context.Background(), wsURL)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close(0, "") })
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: client})
		send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
		readUntil(t, c, protocol.WSWelcome)
		return c
	}
	a := dial("A")
	b := dial("B")
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "rotate"})
	if got := readUntil(t, b, protocol.WSEdit); got.Op != "rotate" || got.FromClientID != "A" {
		t.Fatalf("ws peer got %+v", got)
	}
}
