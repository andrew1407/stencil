package httpapi

// DELETE /projects/{id} is refused while two or more clients share the live
// edit session.

import (
	"encoding/json"
	"net/http"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// mockCounter is a stand-in for the hub's live-connection count.
type mockCounter struct{ n int }

func (f mockCounter) ConnectionCount(string) int { return f.n }

// deleteGuardAPI wires the API with a fixed live-connection count for the delete guard.
func deleteGuardAPI(t *testing.T, connections int) (*API, *testutil.MemStore) {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), LiveSessions: mockCounter{connections}, AdminToken: testAdmin})
	return api, st
}

// TestDeleteRefusedWhileMultipleClientsConnected pins the delete rule: a project is a
// shared workspace (anyone may list/read/edit), and deletion is refused (409) while two
// or more clients are in its live edit session, so a peer can't delete it out from under
// the others.
func TestDeleteRefusedWhileMultipleClientsConnected(t *testing.T) {
	api, _ := deleteGuardAPI(t, 2) // two clients connected
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Busy","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID, tok, nil); r.Code != http.StatusConflict {
		t.Fatalf("delete with 2 live clients should 409, got %d", r.Code)
	}
	// The project survives the refused delete.
	if r := do(t, api, http.MethodGet, "/projects/"+p.ID, tok, nil); r.Code != http.StatusOK {
		t.Fatalf("project should survive a refused delete, got %d", r.Code)
	}
}

// TestDeleteAllowedWithAtMostOneClient: with 0 or 1 live connections, the lone editor
// (or nobody) may delete the project.
func TestDeleteAllowedWithAtMostOneClient(t *testing.T) {
	for _, conns := range []int{0, 1} {
		api, _ := deleteGuardAPI(t, conns)
		tok := issueToken(t, api, "")
		rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Solo","hasImage":true}`))
		var p protocol.ProjectRecord
		json.Unmarshal(rec.Body.Bytes(), &p)
		if r := do(t, api, http.MethodDelete, "/projects/"+p.ID, tok, nil); r.Code != http.StatusNoContent {
			t.Fatalf("delete with %d live clients should 204, got %d", conns, r.Code)
		}
	}
}
