package httpapi

import (
	"net/http"
	"testing"

	"stencil/server/internal/eventbus"
)

// DELETE of an id that names no project is 404, and nothing is published for it.
func TestDeleteOfAMissingProjectIs404(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	events, unsub := api.deps.Bus.Subscribe(eventbus.ChannelEvents)
	t.Cleanup(unsub)
	if rec := do(t, api, http.MethodDelete, "/projects/p_nope", tok, nil); rec.Code != http.StatusNotFound {
		t.Fatalf("delete missing: code %d, want 404", rec.Code)
	}
	select {
	case env := <-events:
		t.Fatalf("a missing id was announced: %s", env.Data)
	default:
	}
}
