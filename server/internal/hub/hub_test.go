package hub

import (
	"context"
	"encoding/json"
	"net"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/transport"
)

// fakeHubStore is an in-memory hub.Store with last-writer-wins semantics.
type fakeHubStore struct {
	mu  sync.Mutex
	rec protocol.ProjectRecord
}

func (f *fakeHubStore) GetProject(_ context.Context, id string) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if id != f.rec.ID {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	return f.rec, nil
}

func (f *fakeHubStore) UpdateProject(_ context.Context, id string, _ *string, _ *string, layout json.RawMessage, expected int64) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if id != f.rec.ID {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if f.rec.Version != expected {
		return protocol.ProjectRecord{}, store.ErrConflict
	}
	if len(layout) > 0 {
		f.rec.Layout = layout
	}
	f.rec.Version++
	return f.rec, nil
}

// fakeResolver accepts exactly one good token.
type fakeResolver struct{ goodHash []byte }

func (f fakeResolver) ResolveToken(_ context.Context, hash []byte) (auth.Session, error) {
	if auth.ConstantTimeEqual(hash, f.goodHash) {
		return auth.Session{ID: "s1", ExpiresAt: 0}, nil
	}
	return auth.Session{}, auth.ErrInvalidToken
}

const goodToken = "good-token"

func newTestHub(t *testing.T) *Hub {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	st := &fakeHubStore{rec: protocol.ProjectRecord{ID: "p_t_a", Name: "P", Version: 0}}
	return New(ctx, st, bus.NewInProc(), fakeResolver{goodHash: auth.HashToken(goodToken)})
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

// joinProject connects, sends hello + subscribe, and waits for welcome.
func joinProject(t *testing.T, addr, project, client string) transport.Conn {
	t.Helper()
	c, err := transport.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: project, ClientID: client})
	send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
	readUntil(t, c, protocol.WSWelcome)
	return c
}

func TestEditFanoutBetweenPeers(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)

	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")

	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "addLine", Payload: json.RawMessage(`{"x":1}`)})

	got := readUntil(t, b, protocol.WSEdit)
	if got.FromClientID != "A" || got.Op != "addLine" {
		t.Fatalf("peer B got wrong edit: %+v", got)
	}
}

func TestSaveLWWAndBroadcast(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")

	// A saves at version 0 -> ack with version 1; B sees synced.
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"lines":[1]}`)})
	ack := readUntil(t, a, protocol.WSSynced)
	if ack.Version != 1 {
		t.Fatalf("save ack version = %d, want 1", ack.Version)
	}
	if bSynced := readUntil(t, b, protocol.WSSynced); bSynced.Version != 1 {
		t.Fatalf("peer synced version = %d", bSynced.Version)
	}

	// A saves again at stale version 0 -> conflict error.
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"lines":[2]}`)})
	e := readUntil(t, a, protocol.WSError)
	if e.Code != protocol.CodeConflict {
		t.Fatalf("expected conflict, got %q", e.Code)
	}
}

func TestEventsFeedReceivesSave(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)

	// Events client: hello with empty ProjectID selects the global feed.
	ev, err := transport.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ev.Close(0, "") })
	send(t, ev, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken})

	a := joinProject(t, addr, "p_t_a", "A")
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{}`)})

	got := readUntil(t, ev, protocol.WSProjectEv)
	if got.Event != protocol.EventUpdated || got.Project == nil || got.Project.ID != "p_t_a" {
		t.Fatalf("events feed got %+v", got)
	}
}

func TestUnauthorizedRejected(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, err := transport.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: "bad", ProjectID: "p_t_a"})
	got := readUntil(t, c, protocol.WSError)
	if got.Code != protocol.CodeUnauthorized {
		t.Fatalf("expected unauthorized, got %q", got.Code)
	}
}

func TestHelloRequiredFirst(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, _ := transport.DialTCP(addr)
	defer c.Close(0, "")
	// Send a non-hello first frame; the server must close the connection.
	send(t, c, protocol.WSMessage{Type: protocol.WSEdit})
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected connection close after non-hello first frame")
	}
}

// TestWebSocketTransport exercises the same flow over the WS adapter.
func TestWebSocketTransport(t *testing.T) {
	h := newTestHub(t)
	srv := httptest.NewServer(h.WSHandler())
	t.Cleanup(srv.Close)
	wsURL := "ws" + strings.TrimPrefix(srv.URL, "http")

	dial := func(client string) transport.Conn {
		c, err := transport.DialWS(context.Background(), wsURL)
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
