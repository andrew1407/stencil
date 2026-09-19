package httpapi

// The compensating deletes: a project row swept mid-upload must not leave the
// bytes behind, and the cleanup must not take a sibling kind with it.

import (
	"encoding/json"
	"net/http"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// An upload whose project row is swept mid-flight must answer 404 and leave no bytes. original/result
// notice through the failing SetFile, a filestore-only kind through the handler's post-write re-check.
func TestUploadOnSweptProjectCleansUpBytes(t *testing.T) {
	cases := []struct {
		name, kind, ext, query string
		body                   []byte
		sweep                  func(*testutil.MemStore, string)
	}{
		{name: "original: SetFile reports the row gone", kind: "original", ext: "png",
			query: "&w=2&h=2", body: []byte{0x89, 0x50, 1, 2},
			sweep: func(st *testutil.MemStore, id string) { st.SweepOnWrite(id) }},
		{name: "video: the post-write re-check finds it gone", kind: "video", ext: "mp4",
			body:  []byte("stub mp4"),
			sweep: func(st *testutil.MemStore, id string) { st.GoneAfterGets(id, 1) }},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			fs, err := filestore.New(t.TempDir())
			if err != nil {
				t.Fatal(err)
			}
			st := testutil.NewMemStore()
			api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
			tok := issueToken(t, api, "")

			rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"doomed","hasImage":true}`))
			var p protocol.ProjectRecord
			json.Unmarshal(rec.Body.Bytes(), &p)
			tc.sweep(st, p.ID)

			up := do(t, api, http.MethodPost,
				"/projects/"+p.ID+"/files/"+tc.kind+"?ext="+tc.ext+tc.query, tok, tc.body)
			if up.Code != http.StatusNotFound {
				t.Fatalf("swept-mid-upload should 404, got %d", up.Code)
			}
			if _, err := fs.Get(p.ID, tc.kind, tc.ext); err == nil {
				t.Fatal("orphaned bytes: the file should have been removed on the swept-write path")
			}
		})
	}
}

// The mid-upload cleanup above must drop only the bytes that upload wrote — it used to
// delete the whole project directory, taking every other kind with it.
func TestFileUploadCleanupLeavesSiblingKindsAlone(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"busy","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	// Land an original and a chat transcript first; both must survive.
	if up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/original?ext=png&w=2&h=2", tok, []byte{0x89, 0x50, 1, 2}); up.Code != http.StatusCreated {
		t.Fatalf("seed original: %d %s", up.Code, up.Body.String())
	}
	if up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/chat?ext=json", tok, []byte(`{"version":1,"messages":[]}`)); up.Code != http.StatusCreated {
		t.Fatalf("seed chat: %d %s", up.Code, up.Body.String())
	}

	// Now lose the race on a variant upload.
	st.GoneAfterGets(p.ID, 1)
	if up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/variant1?ext=png", tok, []byte{0x89, 0x50, 3, 4}); up.Code != http.StatusNotFound {
		t.Fatalf("swept-mid-upload should 404, got %d", up.Code)
	}

	if _, err := fs.Get(p.ID, "variant1", "png"); err == nil {
		t.Fatal("the variant that lost the race should have been removed")
	}
	if _, err := fs.Get(p.ID, "original", "png"); err != nil {
		t.Fatalf("the original must survive a failed variant upload: %v", err)
	}
	if _, err := fs.Get(p.ID, "chat", "json"); err != nil {
		t.Fatalf("the chat transcript must survive a failed variant upload: %v", err)
	}
}
