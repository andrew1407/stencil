package hub

// One session per project: edits and saves reach peers, the events feed sees the
// save, and two projects never hear each other.

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

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
	ev, err := testutil.DialTCP(addr)
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

// TestRoomIsolation: an edit in project A is never delivered to a peer joined to a
// different project B (per-project bus channels — no cross-room message injection).
func TestRoomIsolation(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	// "p_other" is unknown to the mock store (unowned) so joining is allowed; it is a
	// distinct room from "p_t_a".
	other := joinProject(t, addr, "p_other", "B")

	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "addLine", Payload: json.RawMessage(`{"x":1}`)})

	// The other-room peer must NOT receive A's edit. Give it a moment, then assert no
	// edit frame arrived by issuing a ping and expecting the pong first.
	send(t, other, protocol.WSMessage{Type: protocol.WSPing})
	deadline := time.Now().Add(2 * time.Second)
	for {
		ctx, cancel := context.WithDeadline(context.Background(), deadline)
		raw, err := other.Read(ctx)
		cancel()
		if err != nil {
			t.Fatalf("other-room read: %v", err)
		}
		var m protocol.WSMessage
		if json.Unmarshal(raw, &m) != nil {
			continue
		}
		if m.Type == protocol.WSEdit {
			t.Fatalf("cross-room leak: project-B peer received project-A's edit")
		}
		if m.Type == protocol.WSPong {
			break // reached our own pong with no edit before it → isolated
		}
	}
}
