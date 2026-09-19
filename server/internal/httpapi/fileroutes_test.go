// File up/download over REST, including the compensating deletes that keep a
// swept project from orphaning bytes.
package httpapi

import (
	"bytes"
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// Client-uploaded bytes on our own origin: the declared type must be pinned, not sniffed.
func TestFileDownloadSetsNosniff(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"p","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)
	if up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/chat?ext=json", tok, []byte(`{"version":1,"messages":[]}`)); up.Code != http.StatusCreated {
		t.Fatalf("upload chat: %d %s", up.Code, up.Body.String())
	}

	got := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/chat", tok, nil)
	if got.Code != http.StatusOK {
		t.Fatalf("download: %d %s", got.Code, got.Body.String())
	}
	if h := got.Header().Get("X-Content-Type-Options"); h != "nosniff" {
		t.Fatalf("X-Content-Type-Options = %q, want nosniff", h)
	}
	if ct := got.Header().Get("Content-Type"); ct != "application/json" {
		t.Fatalf("Content-Type = %q, want application/json", ct)
	}
}

func TestFileUploadDownload(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Img","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	png := []byte("\x89PNG\r\n stub")
	rec = do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/original?ext=png&w=320&h=240", tok, png)
	if rec.Code != http.StatusCreated {
		t.Fatalf("put file: code %d body %s", rec.Code, rec.Body.String())
	}
	var wr protocol.FileWriteResponse
	json.Unmarshal(rec.Body.Bytes(), &wr)
	if wr.W != 320 || wr.H != 240 || !strings.HasSuffix(wr.Path, "original.png") {
		t.Fatalf("bad write response: %+v", wr)
	}

	// Download round-trips the bytes.
	rec = do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/original", tok, nil)
	if rec.Code != http.StatusOK || !bytes.Equal(rec.Body.Bytes(), png) {
		t.Fatalf("download mismatch: code %d", rec.Code)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "image/png" {
		t.Fatalf("content type %q", ct)
	}

	// Bad kind rejected.
	if rec := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/secret?ext=png", tok, png); rec.Code != http.StatusBadRequest {
		t.Fatalf("bad kind should 400, got %d", rec.Code)
	}
}

// v1 semantics of the LLM-era file kinds: video/variantN bytes round-trip through the filestore, but
// SetFile is never called (no dimensions or path on the project record).
func TestVideoAndVariantUploadSkipsProjectRecord(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Media","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	for kind, payload := range map[string][]byte{
		"video":    []byte("stub mp4 bytes"),
		"variant1": []byte("\x89PNG variant one"),
	} {
		up := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/"+kind+"?ext=bin", tok, payload)
		if up.Code != http.StatusCreated {
			t.Fatalf("put %s: code %d body %s", kind, up.Code, up.Body.String())
		}
		down := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/"+kind, tok, nil)
		if down.Code != http.StatusOK || !bytes.Equal(down.Body.Bytes(), payload) {
			t.Fatalf("get %s: code %d", kind, down.Code)
		}
	}

	calls := st.SetFileCalls()
	after, _ := st.Project(p.ID)
	if calls != 0 {
		t.Fatalf("video/variant uploads must not call SetFile, got %d calls", calls)
	}
	if after.OriginalPath != "" || after.ResultPath != "" || after.Version != 0 {
		t.Fatalf("project record must be untouched: %+v", after)
	}

	// Out-of-range variant slots stay rejected.
	if r := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/variant9?ext=png", tok, []byte("x")); r.Code != http.StatusBadRequest {
		t.Fatalf("variant9 should 400, got %d", r.Code)
	}
	// A never-uploaded variant kind 404s on download.
	if r := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/variant2", tok, nil); r.Code != http.StatusNotFound {
		t.Fatalf("missing variant2 should 404, got %d", r.Code)
	}
}

// The persisted-chat kind (llm-contract.md §12): the JSON document round-trips through the filestore-only
// branch, and the per-file DELETE removes it idempotently while refusing record-backed kinds.
func TestChatFileLifecycle(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Chatty","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	doc := []byte(`{"version":1,"savedAt":1,"messages":[{"role":"user","text":"hi"}]}`)
	if r := do(t, api, http.MethodPost, "/projects/"+p.ID+"/files/chat?ext=json", tok, doc); r.Code != http.StatusCreated {
		t.Fatalf("put chat: code %d body %s", r.Code, r.Body.String())
	}
	down := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/chat", tok, nil)
	if down.Code != http.StatusOK || !bytes.Equal(down.Body.Bytes(), doc) {
		t.Fatalf("get chat: code %d", down.Code)
	}
	if ct := down.Header().Get("Content-Type"); ct != "application/json" {
		t.Fatalf("chat content type %q", ct)
	}

	calls := st.SetFileCalls()
	after, _ := st.Project(p.ID)
	if calls != 0 || after.Version != 0 {
		t.Fatalf("chat upload must not touch the project record: calls=%d rec=%+v", calls, after)
	}

	// DELETE removes the bytes; a repeat delete stays 204 (idempotent).
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/chat", tok, nil); r.Code != http.StatusNoContent {
		t.Fatalf("delete chat: code %d body %s", r.Code, r.Body.String())
	}
	if r := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/chat", tok, nil); r.Code != http.StatusNotFound {
		t.Fatalf("chat should be gone, got %d", r.Code)
	}
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/chat", tok, nil); r.Code != http.StatusNoContent {
		t.Fatalf("repeat delete should stay 204, got %d", r.Code)
	}

	// Record-backed kinds and unknown kinds are refused; auth still gates.
	for _, kind := range []string{"original", "result"} {
		if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/"+kind, tok, nil); r.Code != http.StatusBadRequest {
			t.Fatalf("delete %s should 400, got %d", kind, r.Code)
		}
	}
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/secret", tok, nil); r.Code != http.StatusBadRequest {
		t.Fatalf("delete unknown kind should 400, got %d", r.Code)
	}
	if r := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/chat", "", nil); r.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated delete should 401, got %d", r.Code)
	}
	if r := do(t, api, http.MethodDelete, "/projects/p_missing_zz/files/chat", tok, nil); r.Code != http.StatusNotFound {
		t.Fatalf("delete on missing project should 404, got %d", r.Code)
	}
}

// A filestore-only download reads the project row and the stored path at once (files.go lookupFile); the
// row still decides a 404, whichever read finishes first.
func TestFilestoreOnlyKindOnMissingProjectIsProjectNotFound(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	for _, kind := range []string{"video", "variant1", "chat"} {
		rec := do(t, api, http.MethodGet, "/projects/p_missing_a/files/"+kind, tok, nil)
		if rec.Code != http.StatusNotFound {
			t.Fatalf("%s: code %d", kind, rec.Code)
		}
		var resp protocol.ErrorResponse
		json.Unmarshal(rec.Body.Bytes(), &resp)
		if resp.Message != msgProjectNotFound {
			t.Fatalf("%s: message %q, want %q", kind, resp.Message, msgProjectNotFound)
		}
	}
}

// An upload that would push the filestore past STORAGE_QUOTA_BYTES is refused
// with 507; one that fits is stored, and replacing a file re-uses its budget.
func TestUploadAgainstStorageQuota(t *testing.T) {
	fs, err := filestore.NewWithQuota(t.TempDir(), 1024)
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Q","source":"s","hasImage":true}`))
	if rec.Code != http.StatusCreated {
		t.Fatalf("create: code %d", rec.Code)
	}
	var created protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &created)

	under := make([]byte, 600)
	if rec := do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/original?ext=png&w=1&h=1", tok, under); rec.Code != http.StatusCreated {
		t.Fatalf("under-quota upload: code %d body %s", rec.Code, rec.Body.String())
	}
	over := make([]byte, 600) // 600 + 600 > 1024
	rec = do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/result?ext=png", tok, over)
	if rec.Code != http.StatusInsufficientStorage {
		t.Fatalf("over-quota upload should 507, got %d body %s", rec.Code, rec.Body.String())
	}
	if rec := do(t, api, http.MethodPost, "/projects/"+created.ID+"/files/original?ext=png&w=1&h=1", tok, under); rec.Code != http.StatusCreated {
		t.Fatalf("same-size replacement should fit, got %d", rec.Code)
	}
}
