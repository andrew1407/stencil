package hub

// Local delivery never rides the bus: a flood past the bus subscriber's buffer still reaches every
// local member, and another instance on the same bus hears each frame exactly once.

import (
	"context"
	"encoding/json"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

const floodEdits = 200

func floodStore(t *testing.T) *testutil.MemStore {
	t.Helper()
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a"})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	return st
}

// readEdits reads n edit frames, failing on a gap in their sequence or a duplicate.
func readEdits(t *testing.T, c transport.Conn, who string, n int) {
	t.Helper()
	for i := range n {
		e := readUntil(t, c, protocol.WSEdit)
		var p struct{ I int }
		if json.Unmarshal(e.Payload, &p) != nil || p.I != i {
			t.Fatalf("%s: edit %d carried %s", who, i, e.Payload)
		}
	}
}

func flood(t *testing.T, c transport.Conn) {
	for i := range floodEdits {
		send(t, c, protocol.WSMessage{Type: protocol.WSEdit, Op: "addLine", Payload: json.RawMessage(`{"i":` + itoa(i) + `}`)})
	}
}

func itoa(i int) string { b, _ := json.Marshal(i); return string(b) }

func TestAFloodReachesEveryLocalMemberPastTheBusBuffer(t *testing.T) {
	st := floodStore(t)
	h := New(context.Background(), st, eventbus.NewInProcBuffered(1), st)
	t.Cleanup(h.Close)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")
	c := joinProject(t, addr, "p_t_a", "C")
	flood(t, a)
	readEdits(t, b, "B", floodEdits)
	readEdits(t, c, "C", floodEdits)
}

func TestAnotherInstanceHearsEachFrameOnce(t *testing.T) {
	st := floodStore(t)
	bus := eventbus.NewInProcBuffered(4 * floodEdits)
	one := New(context.Background(), st, bus, st)
	two := New(context.Background(), st, bus, st)
	t.Cleanup(one.Close)
	t.Cleanup(two.Close)
	a := joinProject(t, startTCP(t, one), "p_t_a", "A")
	b := joinProject(t, startTCP(t, one), "p_t_a", "B")
	r := joinProject(t, startTCP(t, two), "p_t_a", "R")
	flood(t, a)
	send(t, a, protocol.WSMessage{Type: protocol.WSPing})
	readEdits(t, r, "remote R", floodEdits)
	readEdits(t, b, "local B", floodEdits)
	readUntil(t, a, protocol.WSPong)
	// A sentinel edit proves no duplicate of the flood is still queued behind it.
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Payload: json.RawMessage(`{"i":-1}`)})
	for name, c := range map[string]transport.Conn{"local B": b, "remote R": r} {
		if e := readUntil(t, c, protocol.WSEdit); string(e.Payload) != `{"i":-1}` {
			t.Fatalf("%s: a duplicate %s arrived before the sentinel", name, e.Payload)
		}
	}
}
