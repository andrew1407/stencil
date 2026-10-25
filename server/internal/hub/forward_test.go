package hub

import (
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/transport"
)

// Only a well-formed project event reaches the clients: another type, an oversize frame, a malformed
// one or one naming an id the server never allocates is dropped at the hub.
func TestOnlyWellFormedProjectEventsAreForwarded(t *testing.T) {
	ev := func(data string) eventbus.Envelope {
		return eventbus.Envelope{Type: protocol.WSProjectEv, Data: []byte(data)}
	}
	good := `{"type":"project-event","event":"deleted","project":{"id":"p_lq3k_9x"}}`
	for _, tc := range []struct {
		name string
		env  eventbus.Envelope
		want bool
	}{
		{"a project event", ev(good), true},
		{"another envelope type", eventbus.Envelope{Type: "chat", Data: []byte(good)}, false},
		{"a type the frame contradicts", ev(`{"type":"error","project":{"id":"p_lq3k_9x"}}`), false},
		{"no project", ev(`{"type":"project-event","event":"deleted"}`), false},
		{"a forged id", ev(`{"type":"project-event","event":"deleted","project":{"id":"p_1\nlog line"}}`), false},
		{"not JSON", ev(`{"type":`), false},
		{"over the hello cap", ev(`{"type":"project-event","project":{"id":"p_a_b","name":"` +
			strings.Repeat("x", transport.MaxHelloBytes) + `"}}`), false},
	} {
		if got := forwardable(tc.env); got != tc.want {
			t.Errorf("%s: forwardable = %v, want %v", tc.name, got, tc.want)
		}
	}
}
