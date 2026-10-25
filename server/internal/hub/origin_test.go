package hub

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/coder/websocket"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/testutil"
)

// The live feed's Origin rule is applied before the upgrade: a page from an origin the rule refuses
// gets a 403 and never a socket, while no Origin (a native client) and an allowed one upgrade.
func TestTheFeedRefusesAnOriginTheRuleRejects(t *testing.T) {
	allow := func(o string) bool { return o == "" || o == "http://localhost:8080" }
	h := New(context.Background(), eventbus.NewInProc(), testutil.NewMemStore(), WithOriginCheck(allow))
	t.Cleanup(h.Close)
	srv := httptest.NewServer(h.WSHandler())
	t.Cleanup(srv.Close)
	url := "ws" + strings.TrimPrefix(srv.URL, "http")
	for _, tc := range []struct {
		origin string
		ok     bool
	}{{"", true}, {"http://localhost:8080", true}, {"https://evil.example", false}} {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		hdr := http.Header{}
		if tc.origin != "" {
			hdr.Set("Origin", tc.origin)
		}
		c, resp, err := websocket.Dial(ctx, url, &websocket.DialOptions{HTTPHeader: hdr})
		cancel()
		if tc.ok != (err == nil) {
			t.Fatalf("origin %q: dial err %v, want ok=%v", tc.origin, err, tc.ok)
		}
		if err == nil {
			c.Close(websocket.StatusNormalClosure, "")
		} else if resp == nil || resp.StatusCode != http.StatusForbidden {
			t.Fatalf("origin %q: refused without a 403 (%v)", tc.origin, resp)
		}
	}
}

// A page served from the host the upgrade names is the app's own origin: it needs no allowlist entry,
// directly or behind a proxy that forwards the public host. Another host on that proxy does.
func TestTheFeedAdmitsItsOwnOrigin(t *testing.T) {
	h := New(context.Background(), eventbus.NewInProc(), testutil.NewMemStore(), WithOriginCheck(func(string) bool { return false }))
	t.Cleanup(h.Close)
	srv := httptest.NewServer(h.WSHandler())
	t.Cleanup(srv.Close)
	url := "ws" + strings.TrimPrefix(srv.URL, "http")
	for _, tc := range []struct {
		origin, forwarded string
		ok                bool
	}{
		{srv.URL, "", true},
		{"https://app.example", "app.example", true},
		{"https://evil.example", "app.example", false},
		{"https://evil.example", "", false},
	} {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		hdr := http.Header{"Origin": {tc.origin}}
		if tc.forwarded != "" {
			hdr.Set("X-Forwarded-Host", tc.forwarded)
		}
		c, _, err := websocket.Dial(ctx, url, &websocket.DialOptions{HTTPHeader: hdr})
		cancel()
		if tc.ok != (err == nil) {
			t.Fatalf("origin %q via %q: dial err %v, want ok=%v", tc.origin, tc.forwarded, err, tc.ok)
		}
		if err == nil {
			c.Close(websocket.StatusNormalClosure, "")
		}
	}
}
