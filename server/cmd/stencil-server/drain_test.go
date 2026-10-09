package main

// The drain order, driven through the real serve(): a TCP feed is served under the hub's context, not
// a request's, so a drain that kills the hub before CloseAll hangs up before the goodbye is written.

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
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

const drainToken = "good-token"

func TestServeNoticesTCPFeedsBeforeHangingUp(t *testing.T) {
	st := testutil.NewMemStore()
	if _, err := st.CreateSession(context.Background(), auth.HashToken(drainToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	rootCtx, stop := context.WithCancel(context.Background()) // the signal context run() builds
	bus := eventbus.NewInProc()
	h := hub.New(rootCtx, bus, st)
	tcpLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := tcpLn.Addr().String()
	srv := &http.Server{Addr: "127.0.0.1:0"}
	served := make(chan error, 1)
	var sweepWG sync.WaitGroup
	go func() {
		served <- serve(rootCtx, srv, tcpLn, h, addr, "drain test", false, stop, &sweepWG, 10*time.Second)
	}()

	feed := joinFeed(t, addr, bus)
	stop() // SIGTERM: the signal context is cancelled and serve() drains

	if got := awaitType(t, feed, protocol.WSError); got.Code != protocol.CodeShutdown {
		t.Errorf("the feed's last frame was %+v, want the %s notice", got, protocol.CodeShutdown)
	}
	select {
	case err := <-served:
		if err != nil {
			t.Fatalf("serve: %v", err)
		}
	case <-time.After(20 * time.Second):
		t.Fatal("serve did not return")
	}
}

// joinFeed dials the TCP listener, says hello and waits until the feed delivers a probe event.
func joinFeed(t *testing.T, addr string, bus eventbus.Bus) transport.Conn {
	t.Helper()
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	data, _ := json.Marshal(protocol.WSMessage{Type: protocol.WSHello, Token: drainToken, ClientID: "A"})
	if err := c.Write(context.Background(), data); err != nil {
		t.Fatalf("write: %v", err)
	}
	done := make(chan struct{})
	defer close(done)
	go func() {
		for {
			eventbus.PublishProjectEvent(context.Background(), bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_probe"})
			select {
			case <-done:
				return
			case <-time.After(10 * time.Millisecond):
			}
		}
	}()
	for {
		if got := awaitType(t, c, protocol.WSProjectEv); got.Project != nil && got.Project.ID == "p_probe" {
			return c
		}
	}
}

// awaitType reads frames until one of want arrives; a read failure before it means the server hung up
// first, which for the shutdown notice is the bug under test.
func awaitType(t *testing.T, c transport.Conn, want string) protocol.WSMessage {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	for {
		raw, err := c.Read(ctx)
		if err != nil {
			t.Fatalf("read waiting for %q: %v", want, err)
		}
		var m protocol.WSMessage
		if json.Unmarshal(raw, &m) == nil && m.Type == want {
			return m
		}
	}
}
