package httpapi

// The layout leaves the store only for GET /projects/{id}, and the original only by the files route:
// writes answer with metadata, the feed carries metadata, and the file routes never read a full row.

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// noFullReads fails any full-row read, so a route that still needs one shows up as a 500.
type noFullReads struct{ *testutil.MemStore }

func (noFullReads) GetProject(context.Context, string) (protocol.ProjectRecord, error) {
	return protocol.ProjectRecord{}, errors.New("a full row was read")
}

func TestFileRoutesReadMetadataOnly(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	mem := testutil.NewMemStore()
	mem.Seed(protocol.ProjectRecord{ID: "p_meta_a", Layout: json.RawMessage(`{"lines":[` + strings.Repeat("1,", 512) + `1]}`)})
	api := New(Deps{Projects: noFullReads{mem}, Sessions: mem, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")
	for _, r := range []struct {
		method, path string
		body         []byte
		want         int
	}{
		{http.MethodPost, "/projects/p_meta_a/files/original?ext=png&w=1&h=1", []byte{1, 2}, http.StatusCreated},
		{http.MethodPost, "/projects/p_meta_a/files/variant1?ext=png", []byte{3}, http.StatusCreated},
		{http.MethodGet, "/projects/p_meta_a/files/original", nil, http.StatusOK},
		{http.MethodGet, "/projects/p_meta_a/files/variant1", nil, http.StatusOK},
		{http.MethodDelete, "/projects/p_meta_a/files/variant1", nil, http.StatusNoContent},
	} {
		if rec := do(t, api, r.method, r.path, tok, r.body); rec.Code != r.want {
			t.Errorf("%s %s: %d %s, want %d", r.method, r.path, rec.Code, rec.Body.String(), r.want)
		}
	}
}

func TestWritesAndEventsCarryNoPayload(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	events, unsub := api.deps.Bus.Subscribe(eventbus.ChannelEvents)
	defer unsub()
	created := do(t, api, http.MethodPost, "/projects", tok,
		[]byte(`{"name":"P","hasImage":true,"originalContent":"data:image/png;base64,AAAA","layout":{"lines":[1]}}`))
	var p protocol.ProjectRecord
	json.Unmarshal(created.Body.Bytes(), &p)
	updated := do(t, api, http.MethodPut, "/projects/"+p.ID, tok,
		[]byte(`{"layout":{"lines":[2]},"version":`+strconv.FormatInt(p.Version, 10)+`}`))
	if updated.Code != http.StatusOK {
		t.Fatalf("update: %d %s", updated.Code, updated.Body.String())
	}
	for name, body := range map[string]string{"create": created.Body.String(), "update": updated.Body.String()} {
		if strings.Contains(body, "originalContent") || strings.Contains(body, "layout") {
			t.Errorf("%s answered with a payload: %s", name, body)
		}
	}
	for i := 0; i < 2; i++ {
		if env := <-events; strings.Contains(string(env.Data), "originalContent") || strings.Contains(string(env.Data), "layout") {
			t.Errorf("event %d carried a payload: %s", i, env.Data)
		}
	}
	// GET /projects/{id} is the record plus its layout; the original is the files route's alone.
	if full, _ := st.GetProject(context.Background(), p.ID); len(full.Layout) == 0 || full.OriginalPath == "" {
		t.Fatalf("the layout or the original was not stored: %+v", full)
	}
	got := do(t, api, http.MethodGet, "/projects/"+p.ID, tok, nil).Body.String()
	if strings.Contains(got, "originalContent") || !strings.Contains(got, `"layout":{"lines":[2]}`) {
		t.Fatalf("GET /projects/{id} = %s, want the layout and no inline original", got)
	}
	if file := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/original", tok, nil); file.Body.String() != "\x00\x00\x00" {
		t.Fatalf("the inline original was not served by the files route: %q", file.Body.String())
	}
}
