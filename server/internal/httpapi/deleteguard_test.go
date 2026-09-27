package httpapi

// DELETE /projects/{id} is refused while two or more clients share the live
// edit session.

import (
	"context"
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

// mockRemote stands in for the editors other instances have published.
type mockRemote struct{ n int }

func (f mockRemote) RemoteMembers(context.Context, string) (int, error) { return f.n, nil }

// deleteGuardAPI wires the API with a fixed live-connection count for the delete guard.
func deleteGuardAPI(t *testing.T, connections int) (*API, *testutil.MemStore) {
	return deleteGuardAPIWith(t, Deps{LiveSessions: mockCounter{connections}})
}

func deleteGuardAPIWith(t *testing.T, deps Deps) (*API, *testutil.MemStore) {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	deps.Projects, deps.Sessions, deps.Files, deps.Bus, deps.AdminToken = st, st, fs, eventbus.NewInProc(), testAdmin
	return New(deps), st
}

// A project is a shared workspace (anyone may list/read/edit), so deletion is refused with 409 while two
// or more clients are in its live edit session.
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

// Editors on another instance count too: one here and one there share the session, so the delete is
// refused; the lone editor, wherever it is connected, may delete.
func TestDeleteCountsEditorsOnOtherInstances(t *testing.T) {
	for _, tc := range []struct{ here, there, want int }{
		{1, 1, http.StatusConflict}, {0, 2, http.StatusConflict}, {0, 1, http.StatusNoContent},
	} {
		api, _ := deleteGuardAPIWith(t, Deps{LiveSessions: mockCounter{tc.here}, RemoteSessions: mockRemote{tc.there}})
		tok := issueToken(t, api, "")
		rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Spread","hasImage":true}`))
		var p protocol.ProjectRecord
		json.Unmarshal(rec.Body.Bytes(), &p)
		if r := do(t, api, http.MethodDelete, "/projects/"+p.ID, tok, nil); r.Code != tc.want {
			t.Fatalf("%d here + %d elsewhere: delete got %d, want %d", tc.here, tc.there, r.Code, tc.want)
		}
	}
}
