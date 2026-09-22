package main

// The drain order, driven through the real serve(): a TCP editor is served under the hub's context, not
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

func TestServeNoticesTCPEditorsBeforeHangingUp(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a", Name: "P"})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(drainToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	rootCtx, stop := context.WithCancel(context.Background()) // the signal context run() builds
	h := hub.New(rootCtx, st, eventbus.NewInProc(), st)
	tcpLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := tcpLn.Addr().String()
	srv := &http.Server{Addr: "127.0.0.1:0"}
	served := make(chan error, 1)
	var sweepWG sync.WaitGroup
	go func() { served <- serve(rootCtx, srv, tcpLn, h, addr, "drain test", false, stop, &sweepWG) }()

	editor := joinAsEditor(t, addr)
	stop() // SIGTERM: the signal context is cancelled and serve() drains

	if got := awaitError(t, editor); got.Code != protocol.CodeShutdown {
		t.Errorf("the editor's last frame was %+v, want the %s notice", got, protocol.CodeShutdown)
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

// joinAsEditor dials the TCP listener and joins the seeded project.
func joinAsEditor(t *testing.T, addr string) transport.Conn {
	t.Helper()
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	for _, msg := range []protocol.WSMessage{
		{Type: protocol.WSHello, Token: drainToken, ProjectID: "p_t_a", ClientID: "A"},
		{Type: protocol.WSSubscribe},
	} {
		data, _ := json.Marshal(msg)
		if err := c.Write(context.Background(), data); err != nil {
			t.Fatalf("write: %v", err)
		}
	}
	if got := awaitType(t, c, protocol.WSWelcome); got.Type != protocol.WSWelcome {
		t.Fatalf("join: got %+v", got)
	}
	return c
}

func awaitError(t *testing.T, c transport.Conn) protocol.WSMessage {
	t.Helper()
	return awaitType(t, c, protocol.WSError)
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
