package protocol

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

// This package is the single source of truth the browser/desktop/CLI/extension
// clients re-declare by hand. Nothing here exercises server logic — these tests
// pin the two things a client can silently disagree with us about: the accepted
// file-kind allowlist, and the JSON on the wire (field names, and which fields
// vanish when empty). A rename or a dropped/added omitempty is a breaking change
// for four front-ends, so it should fail here first.

// keys returns the top-level JSON object keys v marshals to.
func keys(t *testing.T, v any) map[string]bool {
	t.Helper()
	raw, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	var obj map[string]json.RawMessage
	if err := json.Unmarshal(raw, &obj); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	out := make(map[string]bool, len(obj))
	for k := range obj {
		out[k] = true
	}
	return out
}

// A zero-value ProjectRecord must still carry the fields clients read
// unconditionally, and must omit every optional one. Adding omitempty to (say)
// hasImage would turn a present-but-false into an absent key for four clients.
func TestProjectRecordZeroValueEmitsExactlyTheRequiredFields(t *testing.T) {
	got := keys(t, ProjectRecord{})
	want := map[string]bool{
		"id": true, "name": true, "createdAt": true, "updatedAt": true,
		"hasImage": true, "imageW": true, "imageH": true, "version": true,
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("zero ProjectRecord keys = %v, want %v", got, want)
	}
}

// Every optional field must appear once populated, under the documented name.
func TestProjectRecordOptionalFieldNames(t *testing.T) {
	got := keys(t, ProjectRecord{
		ID: "p1", Name: "N", CreatedAt: 1, UpdatedAt: 2, ExpiresAt: 3,
		HasImage: true, ImageW: 4, ImageH: 5,
		Source: "https://e/i.png", Resource: "https://e/", Color: "#ff0000",
		Keywords: []string{"k"}, Description: "d",
		BlankColor: "#ffffff", Blank: true,
		OriginalPath: "a", ResultPath: "b", OriginalContent: "c",
		Layout: json.RawMessage(`{"lines":[]}`), Version: 7, OwnerSession: "s1",
	})
	for _, k := range []string{
		"id", "name", "createdAt", "updatedAt", "expiresAt", "hasImage", "imageW", "imageH",
		"source", "resource", "color", "keywords", "description", "blankColor", "blank",
		"originalPath", "resultPath", "originalContent", "layout", "version", "ownerSession",
	} {
		if !got[k] {
			t.Errorf("populated ProjectRecord is missing key %q", k)
		}
	}
	if len(got) != 21 {
		t.Errorf("ProjectRecord emitted %d keys, want 21 — a field was added or renamed: %v", len(got), got)
	}
}

// ProjectRecord must survive a round-trip byte-identically: clients read a record
// from a list, hand it back on update, and must not lose fields on the way.
func TestProjectRecordRoundTrip(t *testing.T) {
	in := ProjectRecord{
		ID: "p1", Name: "Nom", CreatedAt: 10, UpdatedAt: 20, ExpiresAt: 0,
		HasImage: true, ImageW: 640, ImageH: 480,
		Source: "https://e/i.png", Keywords: []string{"a", "b"},
		Layout: json.RawMessage(`{"lines":[{"points":[]}]}`), Version: 3,
	}
	raw, err := json.Marshal(in)
	if err != nil {
		t.Fatal(err)
	}
	var out ProjectRecord
	if err := json.Unmarshal(raw, &out); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(in, out) {
		t.Errorf("round-trip lost data:\n in %+v\nout %+v", in, out)
	}
	// Unknown fields from a newer client must be ignored, never an error.
	if err := json.Unmarshal([]byte(`{"id":"p1","futureField":{"x":1}}`), &out); err != nil {
		t.Errorf("unknown field rejected: %v", err)
	}
}

// UpdateProjectRequest's pointer fields are tri-state: absent = unchanged,
// present-and-empty = clear. Collapsing the two would make "clear the
// description" indistinguishable from "leave it alone".
func TestUpdateProjectRequestDistinguishesAbsentFromCleared(t *testing.T) {
	var absent UpdateProjectRequest
	if err := json.Unmarshal([]byte(`{"version":4}`), &absent); err != nil {
		t.Fatal(err)
	}
	if absent.Name != nil || absent.Color != nil || absent.Description != nil ||
		absent.Keywords != nil || absent.BlankColor != nil || absent.ExpiresAt != nil {
		t.Errorf("omitted fields must decode to nil (unchanged), got %+v", absent)
	}
	if absent.Version != 4 {
		t.Errorf("Version = %d, want 4", absent.Version)
	}

	var cleared UpdateProjectRequest
	body := `{"name":"","color":"","description":"","keywords":[],"blankColor":"","expiresAt":0,"version":9}`
	if err := json.Unmarshal([]byte(body), &cleared); err != nil {
		t.Fatal(err)
	}
	for name, isSet := range map[string]bool{
		"name": cleared.Name != nil, "color": cleared.Color != nil,
		"description": cleared.Description != nil, "keywords": cleared.Keywords != nil,
		"blankColor": cleared.BlankColor != nil, "expiresAt": cleared.ExpiresAt != nil,
	} {
		if !isSet {
			t.Errorf("explicit empty %q decoded to nil — a clear would be read as unchanged", name)
		}
	}
	if cleared.Keywords != nil && len(*cleared.Keywords) != 0 {
		t.Errorf("keywords = %v, want empty slice", *cleared.Keywords)
	}
	if cleared.ExpiresAt != nil && *cleared.ExpiresAt != 0 {
		t.Errorf("expiresAt = %d, want 0", *cleared.ExpiresAt)
	}
}

// Layout is json.RawMessage everywhere so the server relays a client's layout
// untouched; it must never be re-ordered or re-encoded in transit.
func TestLayoutRawMessageIsRelayedVerbatim(t *testing.T) {
	payload := `{"z":1,"a":[1,2,3],"nested":{"b":null}}`
	var req CreateProjectRequest
	if err := json.Unmarshal([]byte(`{"layout":`+payload+`}`), &req); err != nil {
		t.Fatal(err)
	}
	raw, err := json.Marshal(ProjectResponse{Layout: req.Layout})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(raw), payload) {
		t.Errorf("layout was re-encoded in transit:\ngot  %s\nwant it to contain %s", raw, payload)
	}
}

// Every non-2xx REST body is an ErrorResponse; both fields are unconditional.
func TestErrorResponseShape(t *testing.T) {
	got := keys(t, ErrorResponse{})
	if !reflect.DeepEqual(got, map[string]bool{"code": true, "message": true}) {
		t.Errorf("ErrorResponse{} emitted %v, want {code, message}", got)
	}
}

// GET /projects always returns a projects array. A nil slice marshals to null,
// which a client doing `for (const p of body.projects)` would throw on. Store
// .ListProjects deliberately starts from `[]protocol.ProjectRecord{}` for that
// reason; this pins the DTO behaviour that makes the precaution necessary.
func TestProjectListResponseNilSliceIsNull(t *testing.T) {
	raw, err := json.Marshal(ProjectListResponse{})
	if err != nil {
		t.Fatal(err)
	}
	if string(raw) != `{"projects":null}` {
		t.Fatalf("got %s", raw)
	}
	raw, err = json.Marshal(ProjectListResponse{Projects: []ProjectRecord{}})
	if err != nil {
		t.Fatal(err)
	}
	if string(raw) != `{"projects":[]}` {
		t.Fatalf("empty list marshalled as %s, want []", raw)
	}
}

func TestTokenAndFileWriteResponseShapes(t *testing.T) {
	if got := keys(t, TokenResponse{}); !reflect.DeepEqual(got, map[string]bool{"token": true, "expiresAt": true}) {
		t.Errorf("TokenResponse{} emitted %v", got)
	}
	// w/h are 0 for non-image kinds (video/chat) and must still be present.
	if got := keys(t, FileWriteResponse{Path: "p"}); !reflect.DeepEqual(got,
		map[string]bool{"path": true, "w": true, "h": true}) {
		t.Errorf("FileWriteResponse emitted %v, want path/w/h", got)
	}
}
