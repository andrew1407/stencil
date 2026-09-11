package httpapi

// The compensating deletes: a project row swept mid-upload must not leave the
// bytes behind, and the cleanup must not take a sibling kind with it.

import (
	"encoding/json"
	"net/http"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// TestFileUploadOnSweptProjectCleansUpBytes covers the sweep-vs-upload race: if the
// project row is deleted (expired + swept) between the upload handler's existence
// check and its SetFile write, the handler must drop the just-written bytes (not
// orphan them in the filestore) and report 404.
func TestFileUploadOnSweptProjectCleansUpBytes(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"doomed","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	// Simulate the sweep deleting the row right after the handler's GetProject check.
	st.SweepOnWrite(p.ID)

	up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/original?ext=png&w=2&h=2", tok, []byte{0x89, 0x50, 1, 2})
	if up.Code != http.StatusNotFound {
		t.Fatalf("swept-mid-upload should 404, got %d", up.Code)
	}
	// The bytes written before SetFile failed must have been cleaned up, not orphaned.
	if _, err := fs.Get(p.ID, "original", "png"); err == nil {
		t.Fatal("orphaned bytes: the file should have been removed on the swept-write path")
	}
}

// TestVideoUploadOnSweptProjectCleansUpBytes is the filestore-only-kind twin of
// the test above: video/variantN uploads never call SetFile, so the handler
// re-checks project existence after writing and must drop the bytes when the
// sweep deleted the row mid-upload.
func TestVideoUploadOnSweptProjectCleansUpBytes(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"doomed","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	// Let the handler's pre-check pass, then report the row gone on the re-check.
	st.GoneAfterGets(p.ID, 1)

	up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/video?ext=mp4", tok, []byte("stub mp4"))
	if up.Code != http.StatusNotFound {
		t.Fatalf("swept-mid-upload should 404, got %d", up.Code)
	}
	if _, err := fs.Get(p.ID, "video", "mp4"); err == nil {
		t.Fatal("orphaned bytes: the video should have been removed on the swept-write path")
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
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), AdminToken: testAdmin})
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
