package hub

import (
	"crypto/tls"
	"encoding/json"
	"net"
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

// The raw-TCP edit channel still completes the hello → subscribe → welcome handshake when the listener
// is wrapped in TLS (as main.go does with TLS_CERT/TLS_KEY) — the live-edit channel is encryptable.
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
	t.Cleanup(func() { c.Close(0, "") })

	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: "tls-1"})
	send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
	readUntil(t, c, protocol.WSWelcome)

	// A second TLS peer should see the first peer's edit fan out over TLS.
	raw2, err := tls.Dial("tcp", ln.Addr().String(), &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatalf("tls dial 2: %v", err)
	}
	c2 := transport.NewTCP(raw2)
	t.Cleanup(func() { c2.Close(0, "") })
	send(t, c2, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: "tls-2"})
	send(t, c2, protocol.WSMessage{Type: protocol.WSSubscribe})
	readUntil(t, c2, protocol.WSWelcome)

	send(t, c, protocol.WSMessage{Type: protocol.WSEdit, Op: "addLine", Payload: json.RawMessage(`{"x":1}`)})
	got := readUntil(t, c2, protocol.WSEdit)
	if got.FromClientID != "tls-1" || got.Op != "addLine" {
		t.Fatalf("peer 2 got wrong edit over TLS: %+v", got)
	}
}
