package hub

// The hello handshake and the frame guards around it: a connection that skips
// hello, stalls, or oversteps the size cap is closed without taking the session with it.

import (
	"context"
	"encoding/json"
	"testing"
	"time"

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
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: "bad", ProjectID: "p_t_a"})
	got := readUntil(t, c, protocol.WSError)
	if got.Code != protocol.CodeUnauthorized {
		t.Fatalf("expected unauthorized, got %q", got.Code)
	}
}

func TestHelloRequiredFirst(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, _ := testutil.DialTCP(addr)
	defer c.Close(0, "")
	// Send a non-hello first frame; the server must close the connection.
	send(t, c, protocol.WSMessage{Type: protocol.WSEdit})
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected connection close after non-hello first frame")
	}
}

// TestHelloTimeoutClosesSilentPeer: a connection that never sends its hello frame
// is closed once helloTimeout elapses, so a peer can't hold a slot open forever.
func TestHelloTimeoutClosesSilentPeer(t *testing.T) {
	prev := helloTimeout
	helloTimeout = 150 * time.Millisecond
	t.Cleanup(func() { helloTimeout = prev })

	h := newTestHub(t)
	addr := startTCP(t, h)
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	// Send nothing. The server must close the connection after the (shortened) timeout.
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected the connection to be closed after the hello timeout")
	}
}

// TestMalformedFrameDoesNotDropSession: a non-JSON frame mid-session is ignored
// (not fatal), and the session keeps working for that peer and its peers.
func TestMalformedFrameDoesNotDropSession(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")

	// A garbage frame from A must be dropped without tearing down the session.
	if err := a.Write(context.Background(), []byte("not json at all {{{")); err != nil {
		t.Fatalf("write garbage: %v", err)
	}
	// A subsequent valid edit from A still fans out to B — the session survived.
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "addLine", Payload: json.RawMessage(`{"x":1}`)})
	if got := readUntil(t, b, protocol.WSEdit); got.FromClientID != "A" || got.Op != "addLine" {
		t.Fatalf("session did not survive a malformed frame: %+v", got)
	}
}

// TestOversizedFrameRejected: a first frame beyond transport.MaxMessageBytes is
// rejected (connection closed) rather than buffered into memory.
func TestOversizedFrameRejected(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close(0, "")
	// One frame just over the cap. The TCP scanner's buffer limit makes the read fail,
	// so HandleConn closes the connection instead of allocating unbounded memory.
	huge := make([]byte, transport.MaxMessageBytes+1024)
	for i := range huge {
		huge[i] = 'a'
	}
	_ = c.Write(context.Background(), huge) // may error as the server tears down; that's fine
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if _, err := c.Read(ctx); err == nil {
		t.Fatal("expected the connection to close on an over-limit frame")
	}
}
