package protocol

// The WebSocket envelope on the wire: the type constants, and which fields
// vanish when empty.

import (
	"encoding/json"
	"reflect"
	"testing"
)

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

// Cursor coordinates are the one place omitempty bites: 0 is a legal position at the image origin and is
// dropped from the wire, so clients must default a missing x/y to 0.
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
