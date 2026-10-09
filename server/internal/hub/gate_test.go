package hub

import (
	"context"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// A hello naming a project is answered with a badRequest error and hung up: no session is served.
func TestHelloNamingAProjectIsRefused(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	for _, id := range []string{"p_t_a", "../etc"} {
		c, err := testutil.DialTCP(addr)
		if err != nil {
			t.Fatal(err)
		}
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: id})
		if got := readUntil(t, c, protocol.WSError); got.Code != protocol.CodeBadRequest || got.Message == "" {
			t.Fatalf("%s: refused with %+v, want %s and a reason", id, got, protocol.CodeBadRequest)
		}
		expectClosed(t, c, id)
		c.Close(0, "")
	}
	waitFor(t, func() bool { return liveConnCount(h) == 0 })
}

// The clientId and name older clients send ride along unread: such a hello is served the feed.
func TestHelloWithClientFieldsIsServed(t *testing.T) {
	h := newTestHub(t)
	c, err := testutil.DialTCP(startTCP(t, h))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	hello := `{"type":"hello","token":"` + goodToken + `","clientId":"desktop-1","name":"` + strings.Repeat("n", 200) + `"}`
	if err := c.Write(context.Background(), []byte(hello)); err != nil {
		t.Fatal(err)
	}
	awaitFeed(t, h, c)
}

// A connection outlives nothing its token does: at the session's expiry the peer is told why and hung up.
func TestConnectionEndsWhenItsTokenExpires(t *testing.T) {
	h := newTestHub(t)
	st := h.resolver.(*testutil.MemStore)
	addr := startTCP(t, h)
	expires := clock.NowMs() + 300
	if _, err := st.CreateSession(context.Background(), auth.HashToken("short"), "t", 0, expires); err != nil {
		t.Fatal(err)
	}
	c, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(0, "") })
	send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: "short"})
	got := readUntil(t, c, protocol.WSError)
	if got.Code != protocol.CodeUnauthorized || got.Message != tokenExpiredNotice.Message {
		t.Fatalf("last frame %+v, want %s", got, protocol.CodeUnauthorized)
	}
	if now := clock.NowMs(); now < expires {
		t.Fatalf("hung up %d ms before the token expired", expires-now)
	}
	expectClosed(t, c, "token expiry")
	waitFor(t, func() bool { return liveConnCount(h) == 0 })
}

// A token that never expires (ExpiresAt 0) arms nothing.
func TestExpireAtZeroNeverFires(t *testing.T) {
	fired := make(chan struct{}, 1)
	stop := newTestHub(t).expireAt(0, nil, func() { fired <- struct{}{} })
	defer stop()
	select {
	case <-fired:
		t.Fatal("a never-expiring token hung up")
	case <-time.After(50 * time.Millisecond):
	}
}
