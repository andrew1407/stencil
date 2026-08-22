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

func TestIsVariantKindAcceptsExactlyOneThroughEight(t *testing.T) {
	for _, kind := range []string{"variant1", "variant2", "variant3", "variant4",
		"variant5", "variant6", "variant7", "variant8"} {
		if !IsVariantKind(kind) {
			t.Errorf("IsVariantKind(%q) = false, want true", kind)
		}
	}
	// The cap deliberately matches the op-plan variant cap in llm-contract.md, so
	// the boundaries on both sides matter.
	for _, kind := range []string{
		"variant0",  // below the range
		"variant9",  // one past the cap
		"variant10", // two digits: length check must reject, not truncate to "variant1"
		"variant",   // the bare prefix
		"variant ",  // trailing space
		"Variant1",  // case-sensitive
		"variantA",  // non-digit
		"avariant1", // prefix not anchored at the start
		"variant1x",
		"",
	} {
		if IsVariantKind(kind) {
			t.Errorf("IsVariantKind(%q) = true, want false", kind)
		}
	}
}

func TestIsFileKindAllowlist(t *testing.T) {
	for _, kind := range []string{KindOriginal, KindResult, KindVideo, KindChat, "variant1", "variant8"} {
		if !IsFileKind(kind) {
			t.Errorf("IsFileKind(%q) = false, want true", kind)
		}
	}
	for _, kind := range []string{"", "layout", "thumbnail", "ORIGINAL", "variant9", "../original"} {
		if IsFileKind(kind) {
			t.Errorf("IsFileKind(%q) = true, want false", kind)
		}
	}
}

// The per-file DELETE route removes only filestore-only kinds; original/result go
// with the project. Two invariants matter more than the individual answers.
func TestIsFilestoreOnlyKindIsASubsetThatExcludesOriginalAndResult(t *testing.T) {
	for _, kind := range []string{KindVideo, KindChat, "variant1", "variant8"} {
		if !IsFilestoreOnlyKind(kind) {
			t.Errorf("IsFilestoreOnlyKind(%q) = false, want true", kind)
		}
	}
	if IsFilestoreOnlyKind(KindOriginal) || IsFilestoreOnlyKind(KindResult) {
		t.Error("original/result must not be filestore-only: they are removed with the project")
	}
	// Anything deletable must first be an accepted kind, or the DELETE route would
	// accept a path the filestore rejects.
	for _, kind := range []string{KindOriginal, KindResult, KindVideo, KindChat,
		"variant1", "variant8", "variant9", "", "layout"} {
		if IsFilestoreOnlyKind(kind) && !IsFileKind(kind) {
			t.Errorf("%q is filestore-only but not an accepted file kind", kind)
		}
	}
}

// The kind constants are wire values echoed in REST paths; renaming the Go
// identifier is fine, changing the string is not.
func TestKindConstantWireValues(t *testing.T) {
	got := map[string]string{
		"KindOriginal": KindOriginal,
		"KindResult":   KindResult,
		"KindVideo":    KindVideo,
		"KindChat":     KindChat,
	}
	want := map[string]string{
		"KindOriginal": "original",
		"KindResult":   "result",
		"KindVideo":    "video",
		"KindChat":     "chat",
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("file-kind wire values drifted:\n got %v\nwant %v", got, want)
	}
}

// WS type identifiers and error codes are matched as literals by every client.
func TestWireStringConstants(t *testing.T) {
	got := map[string]string{
		"WSHello": WSHello, "WSSubscribe": WSSubscribe, "WSEdit": WSEdit,
		"WSCursor": WSCursor, "WSPresence": WSPresence, "WSSave": WSSave, "WSPing": WSPing,
		"WSWelcome": WSWelcome, "WSPeerJoin": WSPeerJoin, "WSPeerLeave": WSPeerLeave,
		"WSSynced": WSSynced, "WSError": WSError, "WSPong": WSPong, "WSProjectEv": WSProjectEv,
		"EventCreated": EventCreated, "EventUpdated": EventUpdated, "EventDeleted": EventDeleted,
		"CodeUnauthorized": CodeUnauthorized, "CodeBadVersion": CodeBadVersion,
		"CodeNotFound": CodeNotFound, "CodeConflict": CodeConflict,
		"CodeBadRequest": CodeBadRequest, "CodeInternal": CodeInternal,
		"CodeLlmDisabled": CodeLlmDisabled, "CodeRateLimited": CodeRateLimited,
		"CodeLlmUpstream": CodeLlmUpstream,
	}
	want := map[string]string{
		"WSHello": "hello", "WSSubscribe": "subscribe", "WSEdit": "edit",
		"WSCursor": "cursor", "WSPresence": "presence", "WSSave": "save", "WSPing": "ping",
		"WSWelcome": "welcome", "WSPeerJoin": "peer-join", "WSPeerLeave": "peer-leave",
		"WSSynced": "synced", "WSError": "error", "WSPong": "pong", "WSProjectEv": "project-event",
		"EventCreated": "created", "EventUpdated": "updated", "EventDeleted": "deleted",
		"CodeUnauthorized": "unauthorized", "CodeBadVersion": "badVersion",
		"CodeNotFound": "notFound", "CodeConflict": "conflict",
		"CodeBadRequest": "badRequest", "CodeInternal": "internal",
		"CodeLlmDisabled": "llmDisabled", "CodeRateLimited": "rateLimited",
		"CodeLlmUpstream": "llmUpstream",
	}
	if !reflect.DeepEqual(got, want) {
		for k, w := range want {
			if got[k] != w {
				t.Errorf("%s = %q, want %q", k, got[k], w)
			}
		}
	}
}

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

// An empty WSMessage envelope must be just its type: every other field is
// optional per message type, and clients switch on presence.
func TestWSMessageOmitsEveryOptionalField(t *testing.T) {
	got := keys(t, WSMessage{Type: WSPing})
	if !reflect.DeepEqual(got, map[string]bool{"type": true}) {
		t.Errorf("WSMessage{Type} emitted %v, want only {type}", got)
	}
}

// A welcome carries the snapshot; a nil Project must drop the key rather than
// emit a null a client would dereference.
func TestWSMessageWelcomeShape(t *testing.T) {
	rec := ProjectRecord{ID: "p1", Version: 2}
	got := keys(t, WSMessage{
		Type:    WSWelcome,
		Project: &rec,
		Layout:  json.RawMessage(`{"lines":[]}`),
		Version: 2,
		Peers:   []Peer{{ClientID: "A", Name: "Ann"}},
	})
	for _, k := range []string{"type", "project", "layout", "version", "peers"} {
		if !got[k] {
			t.Errorf("welcome is missing key %q", k)
		}
	}
	if keys(t, WSMessage{Type: WSWelcome})["project"] {
		t.Error("a nil Project must be omitted, not serialized as null")
	}
}

// Cursor coordinates are the one place omitempty bites: 0 is a legal position at
// the image origin, and it is dropped from the wire. Clients therefore must
// default a missing x/y to 0 — this test documents that so the behaviour is a
// decision rather than a surprise.
func TestWSMessageCursorOriginIsOmittedAndDecodesBackToZero(t *testing.T) {
	got := keys(t, WSMessage{Type: WSCursor, X: 0, Y: 0})
	if got["x"] || got["y"] {
		t.Errorf("expected x/y at the origin to be omitted, got %v", got)
	}
	raw, err := json.Marshal(WSMessage{Type: WSCursor, X: 0, Y: 12.5})
	if err != nil {
		t.Fatal(err)
	}
	var back WSMessage
	if err := json.Unmarshal(raw, &back); err != nil {
		t.Fatal(err)
	}
	if back.X != 0 || back.Y != 12.5 {
		t.Errorf("cursor round-trip = (%v,%v), want (0,12.5)", back.X, back.Y)
	}
}

// A hello routed over TCP carries its project in-band (no request path to read).
func TestWSMessageHelloCarriesRouteInBand(t *testing.T) {
	raw, err := json.Marshal(WSMessage{
		Type: WSHello, Token: "t", ClientID: "A", Name: "Ann", ProjectID: "p1",
	})
	if err != nil {
		t.Fatal(err)
	}
	var back WSMessage
	if err := json.Unmarshal(raw, &back); err != nil {
		t.Fatal(err)
	}
	if back.ProjectID != "p1" || back.Token != "t" || back.ClientID != "A" {
		t.Errorf("hello lost routing fields: %+v", back)
	}
	// An empty ProjectID selects the global /events feed, so it must be omitted
	// rather than sent as "" — both decode to "", but the wire shape is documented.
	if keys(t, WSMessage{Type: WSHello, Token: "t"})["projectId"] {
		t.Error("an empty ProjectID must be omitted from the hello frame")
	}
}

// The LLM proxy DTOs are shared with llm-contract.md §6.3.
func TestLlmChatRequestShape(t *testing.T) {
	body := `{"system":"sys","messages":[{"role":"user","text":"hi","images":[{"mediaType":"image/png","data":"AAAA"}]}],"model":"m","maxTokens":100}`
	var req LlmChatRequest
	if err := json.Unmarshal([]byte(body), &req); err != nil {
		t.Fatal(err)
	}
	if req.System != "sys" || req.Model != "m" || req.MaxTokens != 100 || len(req.Messages) != 1 {
		t.Fatalf("decoded %+v", req)
	}
	m := req.Messages[0]
	if m.Role != "user" || m.Text != "hi" || len(m.Images) != 1 ||
		m.Images[0].MediaType != "image/png" || m.Images[0].Data != "AAAA" {
		t.Fatalf("decoded message %+v", m)
	}
	// Messages is required (not omitempty): an empty history still sends the key.
	if !keys(t, LlmChatRequest{})["messages"] {
		t.Error("messages must always be present on the wire")
	}
}

// StopReason passes the provider value through verbatim — clients branch on it
// and must never see it normalised away.
func TestLlmChatResponseAlwaysCarriesStopReason(t *testing.T) {
	for _, reason := range []string{"end_turn", "max_tokens", "refusal", ""} {
		got := keys(t, LlmChatResponse{Model: "m", Text: "t", StopReason: reason})
		if !got["stopReason"] || !got["model"] || !got["text"] {
			t.Errorf("LlmChatResponse(stopReason=%q) emitted %v, want model/text/stopReason", reason, got)
		}
	}
}

// Enabled is false when the server has no API key; false must be on the wire, not
// omitted, or a settings UI would read "absent" as "unknown".
func TestLlmInfoResponseAlwaysCarriesEnabled(t *testing.T) {
	got := keys(t, LlmInfoResponse{})
	if !got["enabled"] || !got["model"] {
		t.Errorf("LlmInfoResponse{} emitted %v, want enabled+model always present", got)
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
