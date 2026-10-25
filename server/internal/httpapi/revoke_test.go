package httpapi

import (
	"encoding/json"
	"net/http"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

func sessionOf(t *testing.T, api *API, tok string) string {
	t.Helper()
	var resp protocol.SessionResponse
	_ = json.Unmarshal(do(t, api, http.MethodGet, "/auth/session", tok, nil).Body.Bytes(), &resp)
	return resp.SessionID
}

func revokedID(t *testing.T, events <-chan eventbus.Envelope) string {
	t.Helper()
	select {
	case env := <-events:
		var id string
		if env.Type != eventbus.TypeSessionRevoked || json.Unmarshal(env.Data, &id) != nil {
			t.Fatalf("published %s %s, want a revocation", env.Type, env.Data)
		}
		return id
	case <-time.After(time.Second):
		t.Fatal("no revocation was published")
	}
	return ""
}

// A bearer ends its own session; its token stops resolving and every hub is told.
func TestASessionRevokesItself(t *testing.T) {
	api, _ := testAPI(t, "")
	events, unsub := api.deps.Bus.Subscribe(eventbus.ChannelEvents)
	t.Cleanup(unsub)
	tok := issueToken(t, api, "")
	id := sessionOf(t, api, tok)
	if rec := do(t, api, http.MethodDelete, "/auth/session", tok, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("self revoke: code %d", rec.Code)
	}
	if got := revokedID(t, events); got != id {
		t.Fatalf("revoked %q, want %q", got, id)
	}
	if rec := do(t, api, http.MethodGet, "/auth/session", tok, nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("a revoked token still answers %d", rec.Code)
	}
}

// The admin revokes one session by id, or all of them; nobody else may, and an unknown id is 404.
func TestTheAdminRevokesOneOrEverySession(t *testing.T) {
	api, _ := testAPI(t, "")
	a, b := issueToken(t, api, ""), issueToken(t, api, "")
	idA := sessionOf(t, api, a)
	if rec := do(t, api, http.MethodDelete, "/auth/sessions/"+idA, b, nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("a session revoking another: code %d, want 401", rec.Code)
	}
	if rec := do(t, api, http.MethodDelete, "/auth/sessions/s_nope", testAdmin, nil); rec.Code != http.StatusNotFound {
		t.Fatalf("unknown id: code %d, want 404", rec.Code)
	}
	if rec := do(t, api, http.MethodDelete, "/auth/sessions/"+idA, testAdmin, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("admin revoke: code %d", rec.Code)
	}
	if sessionOf(t, api, a) != "" || sessionOf(t, api, b) == "" {
		t.Fatal("the admin revoke should end A's session and only A's")
	}
	if rec := do(t, api, http.MethodDelete, "/auth/sessions", b, nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("a session revoking all: code %d, want 401", rec.Code)
	}
	if rec := do(t, api, http.MethodDelete, "/auth/sessions", testAdmin, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("revoke all: code %d", rec.Code)
	}
	if sessionOf(t, api, b) != "" {
		t.Fatal("revoke-all left a session standing")
	}
}
