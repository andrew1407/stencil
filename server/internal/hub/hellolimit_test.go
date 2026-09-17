package hub

// The hello handshake is the WS/TCP credential check. It had no limiter, so a
// page could brute-force tokens at line rate over a socket AcceptWS accepts from
// any origin. These cover: failures throttle, the store is never touched once
// the bucket is empty, a good token spends nothing, and one client's failures
// can't starve another's handshakes.

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strconv"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/coder/websocket"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// countingResolver records how many token lookups reached the store.
type countingResolver struct {
	*testutil.MemStore
	lookups atomic.Int64
}

func (c *countingResolver) ResolveToken(ctx context.Context, hash []byte) (auth.Session, error) {
	c.lookups.Add(1)
	return c.MemStore.ResolveToken(ctx, hash)
}

// limitedHub is newTestHub with the hello limiter armed.
func limitedHub(t *testing.T, perMin int, trusted []netip.Prefix) (*Hub, *countingResolver) {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	res := &countingResolver{MemStore: testutil.NewMemStore()}
	res.Seed(protocol.ProjectRecord{ID: "p_t_a", Name: "P", Version: 0})
	if _, err := res.CreateSession(ctx, auth.HashToken(goodToken), "test", 0, 0); err != nil {
		t.Fatal(err)
	}
	return New(ctx, res, eventbus.NewInProc(), res, WithHelloLimit(perMin, trusted)), res
}

// helloOnce dials, sends one hello, and returns the error code it got back.
func helloOnce(t *testing.T, addr, token string) string {
	t.Helper()
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: token, ProjectID: "p_t_a"})
	return readUntil(t, c, protocol.WSError).Code
}

func TestFailedHellosAreThrottledPerIP(t *testing.T) {
	h, res := limitedHub(t, 2, nil)
	addr := startTCP(t, h)

	for i := 0; i < 2; i++ {
		if code := helloOnce(t, addr, "bad"); code != protocol.CodeUnauthorized {
			t.Fatalf("attempt %d: code %q, want %q", i+1, code, protocol.CodeUnauthorized)
		}
	}
	if got := res.lookups.Load(); got != 2 {
		t.Fatalf("the store saw %d lookups, want 2", got)
	}
	// The bucket is empty: the next hello is refused before auth runs.
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: "bad", ProjectID: "p_t_a"})
	if code := readUntil(t, c, protocol.WSError).Code; code != protocol.CodeRateLimited {
		t.Fatalf("a throttled hello should report %q, got %q", protocol.CodeRateLimited, code)
	}
	expectClosed(t, c, "throttled hello")
	if got := res.lookups.Load(); got != 2 {
		t.Fatalf("a throttled hello must not reach the store (%d lookups)", got)
	}
}

// A valid token refunds its token, so ordinary clients never spend the budget —
// however many times they connect.
func TestGoodHelloSpendsNoHelloBudget(t *testing.T) {
	h, _ := limitedHub(t, 2, nil)
	addr := startTCP(t, h)

	for i := 0; i < 6; i++ { // far more than the burst of 2
		joinProject(t, addr, "p_t_a", "c"+strconv.Itoa(i)) // fails the test if no welcome arrives
	}
	// The failure budget is untouched: two failures still authenticate normally.
	for i := 0; i < 2; i++ {
		if code := helloOnce(t, addr, "bad"); code != protocol.CodeUnauthorized {
			t.Fatalf("failure %d: code %q, want %q", i+1, code, protocol.CodeUnauthorized)
		}
	}
}

// wsHello opens a WebSocket with the given X-Forwarded-For, sends one hello and
// returns the error code (testutil.DialWS sets no headers).
func wsHello(t *testing.T, url, xff, token string) string {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	hdr := http.Header{}
	if xff != "" {
		hdr.Set("X-Forwarded-For", xff)
	}
	c, _, err := websocket.Dial(ctx, url, &websocket.DialOptions{HTTPHeader: hdr})
	if err != nil {
		t.Fatalf("ws dial: %v", err)
	}
	defer c.Close(websocket.StatusNormalClosure, "")
	data, _ := json.Marshal(protocol.WSMessage{Type: protocol.WSHello, Token: token, ProjectID: "p_t_a"})
	if err := c.Write(ctx, websocket.MessageText, data); err != nil {
		t.Fatalf("ws write: %v", err)
	}
	_, raw, err := c.Read(ctx)
	if err != nil {
		t.Fatalf("ws read: %v", err)
	}
	var msg protocol.WSMessage
	if err := json.Unmarshal(raw, &msg); err != nil {
		t.Fatalf("ws frame %q: %v", raw, err)
	}
	return msg.Code
}

// Behind a trusted proxy each forwarded client gets its own hello budget; with
// no trusted proxy the header is ignored, so spoofing it buys nothing.
func TestHelloBudgetKeysOnTheForwardedClientOnlyWhenTrusted(t *testing.T) {
	loopback := []netip.Prefix{netip.MustParsePrefix("127.0.0.0/8")}
	for _, tc := range []struct {
		name    string
		trusted []netip.Prefix
		want    string
	}{
		{"trusted proxy: a second client is unaffected", loopback, protocol.CodeUnauthorized},
		{"untrusted peer: the spoofed header shares one bucket", nil, protocol.CodeRateLimited},
	} {
		t.Run(tc.name, func(t *testing.T) {
			h, _ := limitedHub(t, 1, tc.trusted)
			srv := httptest.NewServer(h.WSHandler())
			t.Cleanup(srv.Close)
			url := "ws" + strings.TrimPrefix(srv.URL, "http")

			if code := wsHello(t, url, "198.51.100.1", "bad"); code != protocol.CodeUnauthorized {
				t.Fatalf("first failure: code %q", code)
			}
			if code := wsHello(t, url, "198.51.100.1", "bad"); code != protocol.CodeRateLimited {
				t.Fatalf("the same client's second failure should throttle, got %q", code)
			}
			if code := wsHello(t, url, "203.0.113.9", "bad"); code != tc.want {
				t.Fatalf("other forwarded client: code %q, want %q", code, tc.want)
			}
		})
	}
}
