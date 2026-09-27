package httpapi

// originalHash on the wire: every metadata body and the upload's own answer name the stored original by
// its SHA-256, a same-size replacement changes it, and a project without an original carries none.

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

func sha256Hex(b []byte) string {
	sum := sha256.Sum256(b)
	return hex.EncodeToString(sum[:])
}

// hashesOf reads originalHash from the list row, GET /projects/{id} and the next project-event.
func hashesOf(t *testing.T, api *API, tok, id string, events <-chan eventbus.Envelope) map[string]string {
	t.Helper()
	var list protocol.ProjectListResponse
	json.Unmarshal(do(t, api, http.MethodGet, "/projects", tok, nil).Body.Bytes(), &list)
	var full protocol.ProjectResponse
	json.Unmarshal(do(t, api, http.MethodGet, "/projects/"+id, tok, nil).Body.Bytes(), &full)
	var ev protocol.WSMessage
	select {
	case env := <-events:
		json.Unmarshal(env.Data, &ev)
	case <-time.After(3 * time.Second):
		t.Fatal("no project-event for the upload")
	}
	out := map[string]string{"get": full.Project.OriginalHash, "event": "", "list": ""}
	if ev.Project != nil {
		out["event"] = ev.Project.OriginalHash
	}
	for _, row := range list.Projects {
		if row.ID == id {
			out["list"] = row.OriginalHash
		}
	}
	return out
}

func TestOriginalHashRidesEveryMetadataBody(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	created := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"H","hasImage":true}`))
	if strings.Contains(created.Body.String(), "originalHash") {
		t.Fatalf("a project with no original named a hash: %s", created.Body.String())
	}
	var p protocol.ProjectRecord
	json.Unmarshal(created.Body.Bytes(), &p)
	events, unsub := api.deps.Bus.Subscribe(eventbus.ChannelEvents)
	defer unsub()

	first, second := []byte("\x89PNG first!"), []byte("\x89PNG other!")
	for _, img := range [][]byte{first, second} { // same size, type and dimensions
		up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/original?ext=png&w=8&h=8", tok, img)
		if up.Code != http.StatusCreated {
			t.Fatalf("upload: %d %s", up.Code, up.Body.String())
		}
		want := sha256Hex(img)
		var wrote protocol.FileWriteResponse
		if json.Unmarshal(up.Body.Bytes(), &wrote); wrote.OriginalHash != want {
			t.Errorf("upload response originalHash = %q, want %q", wrote.OriginalHash, want)
		}
		for where, got := range hashesOf(t, api, tok, p.ID, events) {
			if got != want {
				t.Errorf("%s originalHash = %q, want %q", where, got, want)
			}
		}
	}

	if r := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/result?ext=png", tok, []byte("render")); r.Code != http.StatusCreated {
		t.Fatalf("result upload: %d", r.Code)
	}
	upd := do(t, api, http.MethodPut, "/projects/"+p.ID, tok, []byte(`{"layout":{"lines":[]},"version":3}`))
	var after protocol.ProjectRecord
	json.Unmarshal(upd.Body.Bytes(), &after)
	if upd.Code != http.StatusOK || after.OriginalHash != sha256Hex(second) {
		t.Fatalf("update answered %d with hash %q, want the original's", upd.Code, after.OriginalHash)
	}
}

// The original goes only with its project, and the hash with it: the per-file DELETE refuses the kind.
func TestOriginalHashLivesAndDiesWithTheRow(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	var p protocol.ProjectRecord
	json.Unmarshal(do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"D","hasImage":true}`)).Body.Bytes(), &p)
	do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/original?ext=png&w=1&h=1", tok, []byte("img"))
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/original", tok, nil); r.Code != http.StatusBadRequest {
		t.Fatalf("DELETE files/original = %d, want 400", r.Code)
	}
	if rec, _ := st.Project(p.ID); rec.OriginalHash != sha256Hex([]byte("img")) {
		t.Fatalf("a refused delete touched the hash: %q", rec.OriginalHash)
	}
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID, tok, nil); r.Code != http.StatusNoContent {
		t.Fatalf("delete project = %d", r.Code)
	}
	if _, ok := st.Project(p.ID); ok {
		t.Fatal("the row, and its hash, outlived the project")
	}
}
