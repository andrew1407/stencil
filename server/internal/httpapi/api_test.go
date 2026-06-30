package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"sync"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// fakeStore is an in-memory ProjectStore + SessionStore for handler tests.
type fakeStore struct {
	mu       sync.Mutex
	projects map[string]protocol.ProjectRecord
	sessions map[string]auth.Session
	seq      int
}

func newFakeStore() *fakeStore {
	return &fakeStore{
		projects: map[string]protocol.ProjectRecord{},
		sessions: map[string]auth.Session{},
	}
}

func (f *fakeStore) ResolveToken(_ context.Context, hash []byte) (auth.Session, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if s, ok := f.sessions[string(hash)]; ok {
		return s, nil
	}
	return auth.Session{}, auth.ErrInvalidToken
}

func (f *fakeStore) CreateSession(_ context.Context, hash []byte, label string, createdAt, expiresAt int64) (auth.Session, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.seq++
	s := auth.Session{ID: "s_" + strconv.Itoa(f.seq), Label: label, CreatedAt: createdAt, ExpiresAt: expiresAt}
	f.sessions[string(hash)] = s
	return s, nil
}

func (f *fakeStore) ListProjects(context.Context) ([]protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := []protocol.ProjectRecord{}
	for _, p := range f.projects {
		p.Layout = nil
		out = append(out, p)
	}
	return out, nil
}

func (f *fakeStore) GetProject(_ context.Context, id string) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if p, ok := f.projects[id]; ok {
		return p, nil
	}
	return protocol.ProjectRecord{}, store.ErrNotFound
}

func (f *fakeStore) CreateProject(_ context.Context, owner string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.seq++
	id := "p_t" + strconv.FormatInt(int64(f.seq), 36) + "_a"
	rec := protocol.ProjectRecord{
		ID: id, Name: req.Name, CreatedAt: 100, UpdatedAt: 100 + int64(f.seq),
		Source: req.Source, Resource: req.Resource, Color: req.Color, OriginalContent: req.OriginalContent,
		Layout: req.Layout, OwnerSession: owner,
	}
	if rec.Name == "" {
		rec.Name = "Untitled"
	}
	f.projects[id] = rec
	return rec, nil
}

func (f *fakeStore) UpdateProject(_ context.Context, id string, name *string, color *string, layout json.RawMessage, expected int64) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	p, ok := f.projects[id]
	if !ok {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if p.Version != expected {
		return protocol.ProjectRecord{}, store.ErrConflict
	}
	if name != nil {
		p.Name = *name
	}
	if color != nil {
		p.Color = *color
	}
	if len(layout) > 0 {
		p.Layout = layout
	}
	p.Version++
	f.projects[id] = p
	return p, nil
}

func (f *fakeStore) SetFile(_ context.Context, id, kind, rel string, w, h int) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	p, ok := f.projects[id]
	if !ok {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if kind == protocol.KindOriginal {
		p.OriginalPath = rel
		p.HasImage = true
		p.ImageW, p.ImageH = w, h
	} else {
		p.ResultPath = rel
	}
	p.Version++
	f.projects[id] = p
	return p, nil
}

func (f *fakeStore) DeleteProject(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	delete(f.projects, id)
	return nil
}

// testAPI wires the API with fakes plus a real filestore + in-proc bus.
func testAPI(t *testing.T, adminToken string) (*API, *fakeStore) {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := newFakeStore()
	api := New(Deps{
		Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: adminToken,
	})
	return api, st
}

// issueToken mints a token via the API and returns it.
func issueToken(t *testing.T, api *API, admin string) string {
	t.Helper()
	req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
	if admin != "" {
		req.Header.Set("X-Admin-Token", admin)
	}
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

func TestProjectLifecycleHTTP(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	// Create.
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Demo","source":"http://x/a.png"}`))
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

	// Delete.
	if rec := do(t, api, http.MethodDelete, "/projects/"+created.ID, tok, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("delete: code %d", rec.Code)
	}
	if rec := do(t, api, http.MethodGet, "/projects/"+created.ID, tok, nil); rec.Code != http.StatusNotFound {
		t.Fatalf("get deleted should 404, got %d", rec.Code)
	}
}

// TestProjectColorHTTP round-trips the per-project color over REST without a
// database (the fakeStore mirrors the COALESCE semantics): create with a color,
// update it, and clear it with an explicit empty string.
func TestProjectColorHTTP(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")

	// Create carries the color through.
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Tinted","color":"#ff8800"}`))
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
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Img"}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)

	png := []byte("\x89PNG\r\n fake")
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

func TestStrictJSONRejectsUnknownFields(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"x","bogus":1}`))
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("unknown field should 400, got %d", rec.Code)
	}
}
