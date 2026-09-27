package hub

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// The welcome carries the layout once, top-level, and never the original image; a save keeps it
// current even though the store's write answers with metadata only.
func TestWelcomeCarriesTheLayoutNotTheImage(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"lines":[7]}`)})
	readUntil(t, a, protocol.WSSynced)

	b, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { b.Close(0, "") })
	send(t, b, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: "B"})
	send(t, b, protocol.WSMessage{Type: protocol.WSSubscribe})
	w := readUntil(t, b, protocol.WSWelcome)
	if string(w.Layout) != `{"lines":[7]}` || w.Version != 1 {
		t.Fatalf("welcome layout %s at version %d, want the saved one at 1", w.Layout, w.Version)
	}
	if w.Project == nil || w.Project.Layout != nil {
		t.Fatalf("welcome project carried a payload: %+v", w.Project)
	}
}

// An edit with no version (0) is stale once a save has moved the session past it.
func TestVersionZeroEditIsStaleAfterASave(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "fresh"}) // version 0 on a version-0 session
	if got := readUntil(t, b, protocol.WSEdit); got.Op != "fresh" {
		t.Fatalf("peer got %+v", got)
	}
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{}`)})
	readUntil(t, a, protocol.WSSynced)
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "late"})
	if got := readUntil(t, a, protocol.WSError); got.Code != protocol.CodeBadVersion {
		t.Fatalf("a version-0 edit after a save got %+v, want %s", got, protocol.CodeBadVersion)
	}
	send(t, a, protocol.WSMessage{Type: protocol.WSEdit, Op: "current", Version: 1})
	if got := readUntil(t, b, protocol.WSEdit); got.Op != "current" {
		t.Fatalf("a current edit was not relayed: %+v", got)
	}
}

// The welcome's project names the original by its hash, and a save's project-event keeps naming it:
// equal hashes are how a client knows a peer's layout edit is on the picture it already holds.
func TestWelcomeAndSaveEventCarryTheOriginalHash(t *testing.T) {
	const hash = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a", HasImage: true, OriginalHash: hash})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	bus := eventbus.NewInProc()
	h := New(context.Background(), st, bus, st)
	t.Cleanup(h.Close)
	events, unsub := bus.Subscribe(eventbus.ChannelEvents)
	defer unsub()
	c, err := testutil.DialTCP(startTCP(t, h))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: "A"})
	send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
	if w := readUntil(t, c, protocol.WSWelcome); w.Project == nil || w.Project.OriginalHash != hash {
		t.Fatalf("welcome project = %+v, want originalHash %s", w.Project, hash)
	}
	send(t, c, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"lines":[]}`)})
	readUntil(t, c, protocol.WSSynced)
	select {
	case env := <-events:
		var ev protocol.WSMessage
		json.Unmarshal(env.Data, &ev)
		if ev.Project == nil || ev.Project.OriginalHash != hash {
			t.Fatalf("save event = %s, want originalHash %s", env.Data, hash)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("no project-event for the save")
	}
}
