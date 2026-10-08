package hub

// A session's end: a save still in flight when the last member leaves commits AND is announced, Drain
// waits for it, and a project that no longer exists turns its members away instead of welcoming them.

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// holdSaves parks every UpdateProject until release closes, so a test can tear the session down around it.
type holdSaves struct {
	*testutil.MemStore
	entered chan struct{}
	release chan struct{}
}

func (h *holdSaves) UpdateProject(ctx context.Context, id string, p store.ProjectPatch, v int64) (protocol.ProjectRecord, error) {
	h.entered <- struct{}{}
	<-h.release
	return h.MemStore.UpdateProject(ctx, id, p, v)
}

func newHeldHub(t *testing.T) (*Hub, *holdSaves, eventbus.Bus) {
	t.Helper()
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a", Name: "P"})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	held := &holdSaves{MemStore: st, entered: make(chan struct{}, 4), release: make(chan struct{})}
	bus := eventbus.NewInProc()
	h := New(context.Background(), held, bus, st)
	t.Cleanup(h.Close)
	return h, held, bus
}

func TestASaveInFlightWhenTheLastMemberLeavesIsAnnounced(t *testing.T) {
	h, held, bus := newHeldHub(t)
	events, unsub := bus.Subscribe(eventbus.ChannelEvents)
	defer unsub()
	a := joinProject(t, startTCP(t, h), "p_t_a", "A")
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"lines":[1]}`)})
	<-held.entered
	a.Close(0, "bye")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })

	drained := make(chan error, 1)
	go func() { drained <- h.Drain(context.Background()) }()
	select {
	case <-drained:
		t.Fatal("Drain returned while a save was still in the store")
	case <-time.After(50 * time.Millisecond):
	}
	close(held.release)
	select {
	case env := <-events:
		var ev protocol.WSMessage
		if json.Unmarshal(env.Data, &ev) != nil || ev.Event != protocol.EventUpdated || ev.Project.Version != 1 {
			t.Fatalf("feed got %s, want updated at version 1", env.Data)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("the committed save was never announced")
	}
	if err := <-drained; err != nil {
		t.Fatalf("Drain: %v", err)
	}
}

func TestDrainGivesUpAtItsDeadline(t *testing.T) {
	h := newTestHub(t)
	joinProject(t, startTCP(t, h), "p_t_a", "A")
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	if err := h.Drain(ctx); err == nil {
		t.Fatal("Drain returned nil while a member was still live")
	}
}

func TestAJoinWhoseLoadFindsNoProjectIsTurnedAway(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a"})
	st.GoneAfterGets("p_t_a", 1) // the hello's existence check passes; the snapshot load does not
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	h := New(context.Background(), st, eventbus.NewInProc(), st)
	t.Cleanup(h.Close)
	c, err := testutil.DialTCP(startTCP(t, h))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a"})
	send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
	if e := readUntil(t, c, protocol.WSError); e.Code != protocol.CodeNotFound {
		t.Fatalf("got %+v, want notFound", e)
	}
	expectClosed(t, c, "a member of a missing project")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })
}

func TestADeletedProjectEndsItsLiveSession(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	a := joinProject(t, addr, "p_t_a", "A")
	b := joinProject(t, addr, "p_t_a", "B")
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventDeleted, protocol.ProjectRecord{ID: "p_t_a"})
	if e := readUntil(t, a, protocol.WSError); e.Code != protocol.CodeNotFound {
		t.Fatalf("A got %+v, want notFound", e)
	}
	if e := readUntil(t, b, protocol.WSError); e.Code != protocol.CodeNotFound {
		t.Fatalf("B got %+v, want notFound", e)
	}
	expectClosed(t, a, "A after delete")
	expectClosed(t, b, "B after delete")
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })
}
