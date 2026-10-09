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
		"WSHello": WSHello, "WSPing": WSPing,
		"WSError": WSError, "WSPong": WSPong, "WSProjectEv": WSProjectEv,
		"EventCreated": EventCreated, "EventUpdated": EventUpdated, "EventDeleted": EventDeleted,
		"CodeUnauthorized": CodeUnauthorized,
		"CodeNotFound":     CodeNotFound, "CodeConflict": CodeConflict,
		"CodeBadRequest": CodeBadRequest, "CodeInternal": CodeInternal,
		"CodeLlmDisabled": CodeLlmDisabled, "CodeRateLimited": CodeRateLimited,
		"CodeLlmUpstream": CodeLlmUpstream,
	}
	want := map[string]string{
		"WSHello": "hello", "WSPing": "ping",
		"WSError": "error", "WSPong": "pong", "WSProjectEv": "project-event",
		"EventCreated": "created", "EventUpdated": "updated", "EventDeleted": "deleted",
		"CodeUnauthorized": "unauthorized",
		"CodeNotFound":     "notFound", "CodeConflict": "conflict",
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

// A project event carries the record and its sub-type; a nil Project must drop the key rather than
// emit a null a client would dereference.
func TestWSMessageProjectEventShape(t *testing.T) {
	rec := ProjectRecord{ID: "p1", Version: 2}
	got := keys(t, WSMessage{Type: WSProjectEv, Event: EventUpdated, Project: &rec})
	for _, k := range []string{"type", "event", "project"} {
		if !got[k] {
			t.Errorf("project event is missing key %q", k)
		}
	}
	if keys(t, WSMessage{Type: WSProjectEv})["project"] {
		t.Error("a nil Project must be omitted, not serialized as null")
	}
}

// A hello carries its fields in-band (TCP has no request path to read), the project id included so the
// server can refuse it with a reason.
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
