package httpapi

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// testAdmin is the admin token every test API runs with: issuance is always
// gated now (empty AdminToken = closed, not open), so the helpers default to it.
const testAdmin = "test-admin-token"

// testAPI wires the API with shared fakes plus a real filestore + in-proc bus.
// An empty adminToken means "the shared testAdmin", not open issuance.
func testAPI(t *testing.T, adminToken string) (*API, *testutil.MemStore) {
	t.Helper()
	if adminToken == "" {
		adminToken = testAdmin
	}
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{
		Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: adminToken,
	})
	return api, st
}

// issueToken mints a token via the API and returns it. An empty admin defaults
// to the shared testAdmin.
func issueToken(t *testing.T, api *API, admin string) string {
	t.Helper()
	if admin == "" {
		admin = testAdmin
	}
	req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
	req.Header.Set("X-Admin-Token", admin)
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("issue token: code %d body %s", rec.Code, rec.Body.String())
	}
	var resp protocol.TokenResponse
	json.Unmarshal(rec.Body.Bytes(), &resp)
	return resp.Token
}

func do(t *testing.T, api *API, method, path, token string, body []byte) *httptest.ResponseRecorder {
	t.Helper()
	var r *http.Request
	if body != nil {
		r = httptest.NewRequest(method, path, bytes.NewReader(body))
	} else {
		r = httptest.NewRequest(method, path, nil)
	}
	if token != "" {
		r.Header.Set("Authorization", "Bearer "+token)
	}
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, r)
	return rec
}

func TestAuthGate(t *testing.T) {
	api, _ := testAPI(t, "")
	// No token -> 401 on protected route.
	if rec := do(t, api, http.MethodGet, "/projects", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth GET /projects: code %d", rec.Code)
	}
	tok := issueToken(t, api, "")
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("auth GET /projects: code %d", rec.Code)
	}
}

func TestAdminTokenGatesIssuance(t *testing.T) {
	api, _ := testAPI(t, "secret-admin")
	// Wrong/absent admin token -> 401.
	if rec := do(t, api, http.MethodPost, "/auth/token", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("issuance without admin token should be 401, got %d", rec.Code)
	}
	// Correct admin token -> 200.
	tok := issueToken(t, api, "secret-admin")
	if tok == "" {
		t.Fatal("expected a token")
	}
}

// TestOpenAuthIssuance: with AuthOpen (AUTH_OPEN=1) issuance succeeds with no
// bearer at all; the admin bearer keeps working; everything else stays
// token-gated. The default (AuthOpen unset) stays closed — see
// TestAdminTokenGatesIssuance.
func TestOpenAuthIssuance(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: testAdmin, AuthOpen: true})

	// No bearer at all -> 200 with a real session token.
	rec := do(t, api, http.MethodPost, "/auth/token", "", nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("open issuance without bearer: code %d body %s", rec.Code, rec.Body.String())
	}
	var resp protocol.TokenResponse
	json.Unmarshal(rec.Body.Bytes(), &resp)
	if resp.Token == "" {
		t.Fatal("expected a token")
	}
	if rec := do(t, api, http.MethodGet, "/projects", resp.Token, nil); rec.Code != http.StatusOK {
		t.Fatalf("minted token should authenticate: code %d", rec.Code)
	}
	// An explicit admin bearer still works too.
	if tok := issueToken(t, api, testAdmin); tok == "" {
		t.Fatal("admin bearer should still mint")
	}
	// Protected routes remain token-gated.
	if rec := do(t, api, http.MethodGet, "/projects", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth GET /projects should stay 401, got %d", rec.Code)
	}
}

// TestOpenAuthStillRateLimited: the per-IP issuance limiter applies to open
// issuance too.
func TestOpenAuthStillRateLimited(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AuthOpen: true, AuthRatePerMin: 2})

	issue := func() *httptest.ResponseRecorder {
		req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
		req.RemoteAddr = "192.0.2.7:1"
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec
	}
	for i := 0; i < 2; i++ {
		if rec := issue(); rec.Code != http.StatusOK {
			t.Fatalf("open issue %d: code %d body %s", i, rec.Code, rec.Body.String())
		}
	}
	if rec := issue(); rec.Code != http.StatusTooManyRequests {
		t.Fatalf("flood should 429 in open mode, got %d", rec.Code)
	}
}

func TestProjectLifecycleHTTP(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Demo","source":"http://x/a.png","hasImage":true}`))
	if rec.Code != http.StatusCreated {
		t.Fatalf("create: code %d body %s", rec.Code, rec.Body.String())
	}
	var created protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &created)
	if created.ID == "" || created.Name != "Demo" {
		t.Fatalf("bad created: %+v", created)
	}

	// List shows it.
	rec = do(t, api, http.MethodGet, "/projects", tok, nil)
	var list protocol.ProjectListResponse
	json.Unmarshal(rec.Body.Bytes(), &list)
	if len(list.Projects) != 1 {
		t.Fatalf("want 1 project, got %d", len(list.Projects))
	}

	// Update with correct version.
	rec = do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(`{"version":0,"layout":{"lines":[]}}`))
	if rec.Code != http.StatusOK {
		t.Fatalf("update: code %d body %s", rec.Code, rec.Body.String())
	}
	// Stale version -> 409.
	rec = do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(`{"version":0}`))
	if rec.Code != http.StatusConflict {
		t.Fatalf("stale update should 409, got %d", rec.Code)
	}

	// Get returns layout separately.
	rec = do(t, api, http.MethodGet, "/projects/"+created.ID, tok, nil)
	var got protocol.ProjectResponse
	json.Unmarshal(rec.Body.Bytes(), &got)
	if got.Project.Version != 1 || len(got.Layout) == 0 {
		t.Fatalf("get after update: %+v layout=%s", got.Project, got.Layout)
	}

	if rec := do(t, api, http.MethodDelete, "/projects/"+created.ID, tok, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("delete: code %d", rec.Code)
	}
	if rec := do(t, api, http.MethodGet, "/projects/"+created.ID, tok, nil); rec.Code != http.StatusNotFound {
		t.Fatalf("get deleted should 404, got %d", rec.Code)
	}
}

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

// TestProjectExpiryHTTP round-trips the per-project expiry over REST: an explicit
// create-time expiresAt is stored and listed, and an update sets a new one.
func TestProjectExpiryHTTP(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	// Explicit create-time expiry is carried through.
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Temp","expiresAt":5000,"hasImage":true}`))
	var created protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &created)
	if created.ExpiresAt != 5000 {
		t.Fatalf("create-time expiresAt lost: %+v", created)
	}

	// Update sets a new expiry under the version guard.
	rec = do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(`{"version":0,"expiresAt":9000}`))
	if rec.Code != http.StatusOK {
		t.Fatalf("update expiry: code %d body %s", rec.Code, rec.Body.String())
	}
	var upd protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &upd)
	if upd.ExpiresAt != 9000 {
		t.Fatalf("updated expiresAt not applied: %+v", upd)
	}
}

// TestCreateProjectDefaultTTLHTTP checks that when the operator sets a default
// PROJECT_TTL, a create with no explicit expiry is stamped now+TTL, while a create
// that names its own expiry keeps it.
func TestCreateProjectDefaultTTLHTTP(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), ProjectTTL: time.Hour, AdminToken: testAdmin})
	tok := issueToken(t, api, "")

	// No expiry in the body → stamped now + 1h (a large positive value).
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Auto","hasImage":true}`))
	var auto protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &auto)
	if auto.ExpiresAt <= nowMs() {
		t.Fatalf("default TTL not stamped: %+v", auto)
	}

	// An explicit expiry is respected (not overwritten by the default).
	rec = do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Pinned","expiresAt":42,"hasImage":true}`))
	var pinned protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &pinned)
	if pinned.ExpiresAt != 42 {
		t.Fatalf("explicit expiry overwritten by default TTL: %+v", pinned)
	}
}

// TestProjectColorHTTP round-trips the per-project color over REST without a
// database (the mockStore mirrors the COALESCE semantics): create with a color,
// update it, and clear it with an explicit empty string.
func TestProjectColorHTTP(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	// Create carries the color through.
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Tinted","color":"#ff8800","hasImage":true}`))
	if rec.Code != http.StatusCreated {
		t.Fatalf("create: code %d body %s", rec.Code, rec.Body.String())
	}
	var created protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &created)
	if created.Color != "#ff8800" {
		t.Fatalf("create did not carry color: %+v", created)
	}

	// Update sets a new color (name omitted -> unchanged).
	rec = do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(`{"version":0,"color":"#00ff00"}`))
	if rec.Code != http.StatusOK {
		t.Fatalf("color update: code %d body %s", rec.Code, rec.Body.String())
	}
	var updated protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &updated)
	if updated.Color != "#00ff00" || updated.Name != "Tinted" {
		t.Fatalf("color update changed wrong fields: %+v", updated)
	}

	// Explicit empty string clears the color (theme fallback).
	rec = do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(`{"version":1,"color":""}`))
	if rec.Code != http.StatusOK {
		t.Fatalf("color clear: code %d body %s", rec.Code, rec.Body.String())
	}
	// Color has json omitempty, so a cleared "" drops out of the response body;
	// decode into a fresh struct so a stale value cannot mask the clear.
	var cleared protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &cleared)
	if cleared.Color != "" {
		t.Fatalf("empty color should clear, got %q", cleared.Color)
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

// TestVideoAndVariantUploadSkipsProjectRecord pins the v1 semantics of the
// LLM-era file kinds: video/variantN bytes round-trip through the filestore,
// but SetFile is never called (no dimensions/path on the project record).
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

// TestChatFileLifecycle pins the persisted-chat kind (llm-contract.md §12):
// the JSON document round-trips through the filestore-only branch (no SetFile,
// application/json downloads), and the per-file DELETE route removes it
// idempotently while refusing record-backed kinds.
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
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), LiveSessions: mockCounter{connections}, AdminToken: testAdmin})
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

// TestAnyClientCanReadEditList confirms the shared-workspace contract: a second,
// distinct token can list, read, and edit a project created by another token.
func TestAnyClientCanReadEditList(t *testing.T) {
	api, _ := testAPI(t, "")
	tokA := issueToken(t, api, "")
	tokB := issueToken(t, api, "") // a different session/token

	rec := do(t, api, http.MethodPost, "/projects", tokA, []byte(`{"name":"Shared","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	if r := do(t, api, http.MethodGet, "/projects/"+p.ID, tokB, nil); r.Code != http.StatusOK {
		t.Fatalf("any client should read a shared project, got %d", r.Code)
	}
	if r := do(t, api, http.MethodPut, "/projects/"+p.ID, tokB, []byte(`{"version":0,"name":"Edited"}`)); r.Code != http.StatusOK {
		t.Fatalf("any client should edit a shared project, got %d", r.Code)
	}
	lr := do(t, api, http.MethodGet, "/projects", tokB, nil)
	var list protocol.ProjectListResponse
	json.Unmarshal(lr.Body.Bytes(), &list)
	if len(list.Projects) != 1 || list.Projects[0].ID != p.ID {
		t.Fatalf("any client should see the shared project in the list, got %+v", list.Projects)
	}
}

// TestEmptyAdminTokenClosesIssuance pins the fail-closed contract: an API wired
// with NO admin token refuses issuance outright (config.Load generates a per-boot
// token so a real server never runs in this state — but if it does, closed > open).
func TestEmptyAdminTokenClosesIssuance(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc()})
	rec := do(t, api, http.MethodPost, "/auth/token", "", nil)
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("issuance with no admin token configured should 401, got %d", rec.Code)
	}
}

// TestAdminTokenNotUsableAsBearer: the admin token gates issuance but is not itself
// a session — presenting it as a Bearer on a protected route must be rejected (401).
func TestAdminTokenNotUsableAsBearer(t *testing.T) {
	const admin = "secret-admin"
	api, _ := testAPI(t, admin)
	if rec := do(t, api, http.MethodGet, "/projects", admin, nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("admin token used as a session bearer should 401, got %d", rec.Code)
	}
	// A real issued token still works.
	tok := issueToken(t, api, admin)
	if rec := do(t, api, http.MethodGet, "/projects", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("issued token should 200, got %d", rec.Code)
	}
}

// TestCreateRejectsImagelessProject pins the domain rule: a project is created FROM an
// image, so a create request that doesn't declare one (HasImage=false) is refused (400)
// and no project row comes into being. A create that declares an image succeeds.
func TestCreateRejectsImagelessProject(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	// Bare metadata, no image → rejected, and nothing is listed.
	if rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"NoImage"}`)); rec.Code != http.StatusBadRequest {
		t.Fatalf("imageless create should 400, got %d", rec.Code)
	}
	lr := do(t, api, http.MethodGet, "/projects", tok, nil)
	var list protocol.ProjectListResponse
	json.Unmarshal(lr.Body.Bytes(), &list)
	if len(list.Projects) != 0 {
		t.Fatalf("a rejected imageless create must leave no project, got %+v", list.Projects)
	}

	// An image-backed create (as every real client sends) still succeeds.
	if rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"WithImage","hasImage":true}`)); rec.Code != http.StatusCreated {
		t.Fatalf("image-backed create should 201, got %d", rec.Code)
	}
}

func TestStrictJSONRejectsUnknownFields(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"x","bogus":1}`))
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("unknown field should 400, got %d", rec.Code)
	}
}
