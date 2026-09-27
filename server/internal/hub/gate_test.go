package hub

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// A hello naming no existing project (malformed, or well-formed but unknown) is refused with notFound
// before any session is created for it.
func TestHelloForAMissingProjectIsRefused(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	for _, id := range []string{"p_no_such", "../etc", "p_other"} {
		c, err := testutil.DialTCP(addr)
		if err != nil {
			t.Fatal(err)
		}
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: id})
		if got := readUntil(t, c, protocol.WSError); got.Code != protocol.CodeNotFound {
			t.Fatalf("%s: refused with %+v, want %s", id, got, protocol.CodeNotFound)
		}
		expectClosed(t, c, id)
		c.Close(0, "")
		if n := h.ConnectionCount(id); n != 0 || len(h.LiveProjectIDs()) != 0 {
			t.Fatalf("%s: a session was spent on a missing project", id)
		}
	}
}

// The project check applies only when a hello names a project: one naming none is the global feed, as
// ever, and spends no session.
func TestHelloWithoutAProjectStillJoinsTheFeed(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	feed, err := testutil.DialTCP(addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { feed.Close(0, "") })
	send(t, feed, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken})
	a := joinProject(t, addr, "p_t_a", "A") // a full round trip, in which the feed's hello lands
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{}`)})
	if got := readUntil(t, feed, protocol.WSProjectEv); got.Project == nil || got.Project.ID != "p_t_a" {
		t.Fatalf("the feed got %+v", got)
	}
	if ids := h.LiveProjectIDs(); len(ids) != 1 || ids[0] != "p_t_a" {
		t.Fatalf("live projects %v: a feed subscriber must not hold a session", ids)
	}
}

// A connection outlives nothing its token does: at the session's expiry the peer is told why and hung
// up, on the project session and on the events feed alike.
func TestConnectionEndsWhenItsTokenExpires(t *testing.T) {
	h := newTestHub(t)
	st := h.store.(*testutil.MemStore)
	addr := startTCP(t, h)
	for _, project := range []string{"p_t_a", ""} {
		token := "short-" + project
		expires := clock.NowMs() + 300
		if _, err := st.CreateSession(context.Background(), auth.HashToken(token), "t", 0, expires); err != nil {
			t.Fatal(err)
		}
		c, err := testutil.DialTCP(addr)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close(0, "") })
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: token, ProjectID: project})
		got := readUntil(t, c, protocol.WSError)
		if got.Code != protocol.CodeUnauthorized || got.Message != tokenExpiredNotice.Message {
			t.Fatalf("project %q: last frame %+v, want %s", project, got, protocol.CodeUnauthorized)
		}
		if now := clock.NowMs(); now < expires {
			t.Fatalf("project %q: hung up %d ms before the token expired", project, expires-now)
		}
		expectClosed(t, c, "token expiry")
	}
	waitFor(t, func() bool { return h.ConnectionCount("p_t_a") == 0 })
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
