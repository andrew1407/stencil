package hub

import (
	"context"
	"crypto/tls"
	"net"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

// The raw-TCP feed still completes the hello and delivers events when the listener is wrapped in TLS
// (as main.go does with TLS_CERT/TLS_KEY).
func TestTCPTransportOverTLS(t *testing.T) {
	h := newTestHub(t)

	srvConf := testutil.SelfSignedTLS(t)
	base, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	ln := tls.NewListener(base, srvConf)
	t.Cleanup(func() { ln.Close() })
	go h.ServeListener(ln)

	// Client dials over TLS; self-signed, so skip verification in the test only.
	raw, err := tls.Dial("tcp", ln.Addr().String(), &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatalf("tls dial: %v", err)
	}
	if raw.ConnectionState().Version < tls.VersionTLS12 {
		t.Fatalf("negotiated TLS version too low: %x", raw.ConnectionState().Version)
	}
	c := transport.NewTCP(raw)
	c.SetReadLimit(transport.MaxMessageBytes)
	t.Cleanup(func() { c.Close(0, "") })

	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ClientID: "tls-1"})
	awaitFeed(t, h, c)
	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_t_a"})
	if got := readEvent(t, c, "p_t_a"); got.Event != protocol.EventUpdated {
		t.Fatalf("feed over TLS got %+v", got)
	}
}
