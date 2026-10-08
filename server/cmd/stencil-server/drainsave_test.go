package main

// A save the store still holds at SIGTERM: serve() waits for it to commit and be announced before it
// cancels the hub's context, so the store call is never cut short by the shutdown.

import (
	"context"
	"encoding/json"
	"net"
	"net/http"
	"sync"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/hub"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

type heldSave struct {
	*testutil.MemStore
	entered, release chan struct{}
	ctxErr           chan error
}

func (h *heldSave) UpdateProject(ctx context.Context, id string, p store.ProjectPatch, v int64) (protocol.ProjectRecord, error) {
	close(h.entered)
	<-h.release
	h.ctxErr <- ctx.Err()
	return h.MemStore.UpdateProject(ctx, id, p, v)
}

func TestServeWaitsForAnInFlightSaveBeforeCancellingTheHub(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a", Name: "P"})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(drainToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	held := &heldSave{MemStore: st, entered: make(chan struct{}), release: make(chan struct{}), ctxErr: make(chan error, 1)}
	bus := eventbus.NewInProc()
	events, unsub := bus.Subscribe(eventbus.ChannelEvents)
	defer unsub()
	rootCtx, stop := context.WithCancel(context.Background())
	h := hub.New(rootCtx, held, bus, st)
	tcpLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := tcpLn.Addr().String()
	served := make(chan error, 1)
	var sweepWG sync.WaitGroup
	go func() {
		served <- serve(rootCtx, &http.Server{Addr: "127.0.0.1:0"}, tcpLn, h, addr, "drain test", false, stop, &sweepWG, 10*time.Second)
	}()

	editor := joinAsEditor(t, addr)
	data, _ := json.Marshal(protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"n":1}`)})
	if err := editor.Write(context.Background(), data); err != nil {
		t.Fatal(err)
	}
	<-held.entered
	stop()
	awaitError(t, editor) // the shutdown notice; the editor is gone, the save is not
	select {
	case <-served:
		t.Fatal("serve returned while a save was still in the store")
	case <-time.After(100 * time.Millisecond):
	}
	close(held.release)
	if err := <-held.ctxErr; err != nil {
		t.Fatalf("the save's store context ended before it returned: %v", err)
	}
	if err := <-served; err != nil {
		t.Fatalf("serve: %v", err)
	}
	select {
	case env := <-events:
		var ev protocol.WSMessage
		if json.Unmarshal(env.Data, &ev) != nil || ev.Event != protocol.EventUpdated {
			t.Fatalf("feed got %s, want updated", env.Data)
		}
	default:
		t.Fatal("the committed save was never announced")
	}
}
