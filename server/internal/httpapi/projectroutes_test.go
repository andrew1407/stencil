package httpapi

// Project CRUD over REST: the lifecycle, expiry/TTL stamping, colour, and the
// two create-time rejections.

import (
	"encoding/json"
	"net/http"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/clock"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

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
	if auto.ExpiresAt <= clock.NowMs() {
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
