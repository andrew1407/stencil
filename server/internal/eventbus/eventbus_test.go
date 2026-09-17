package eventbus

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"stencil/server/internal/protocol"
)

func recv(t *testing.T, ch <-chan Envelope) []byte {
	t.Helper()
	select {
	case m := <-ch:
		return m.Data
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for message")
		return nil
	}
}

// frame is a stand-in envelope carrying only a payload.
func frame(payload string) Envelope { return Envelope{Data: []byte(payload)} }

func TestInProcFanout(t *testing.T) {
	b := NewInProc()
	defer b.Close()
	ctx := context.Background()

	c1, cancel1 := b.Subscribe("proj:x")
	c2, cancel2 := b.Subscribe("proj:x")
	defer cancel1()
	defer cancel2()

	if err := b.Publish(ctx, "proj:x", frame("hello")); err != nil {
		t.Fatal(err)
	}
	if string(recv(t, c1)) != "hello" || string(recv(t, c2)) != "hello" {
		t.Fatal("both subscribers should receive")
	}
}

func TestInProcChannelIsolation(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	cx, cancelx := b.Subscribe("proj:x")
	cy, cancely := b.Subscribe("proj:y")
	defer cancelx()
	defer cancely()

	b.Publish(ctx, "proj:x", frame("only-x"))
	if string(recv(t, cx)) != "only-x" {
		t.Fatal("x subscriber missed message")
	}
	select {
	case m := <-cy:
		t.Fatalf("y subscriber got cross-channel message %q", m.Data)
	case <-time.After(50 * time.Millisecond):
	}
}

func TestInProcUnsubscribeCloses(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	ch, cancel := b.Subscribe("c")
	cancel()
	if _, open := <-ch; open {
		t.Fatal("channel should be closed after unsubscribe")
	}
	cancel() // idempotent
	// Publishing to a now-empty channel must not panic.
	if err := b.Publish(ctx, "c", frame("x")); err != nil {
		t.Fatal(err)
	}
}

func TestInProcSlowSubscriberDropsNotBlocks(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	_, cancel := b.Subscribe("c") // never drained
	defer cancel()
	// Far more than subBuffer; must not block.
	done := make(chan struct{})
	go func() {
		for i := 0; i < subBuffer*4; i++ {
			b.Publish(ctx, "c", frame("x"))
		}
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("publish blocked on slow subscriber")
	}
}

// The envelope lifts the two routing fields out of the frame, so a subscriber
// never unmarshals the frame again just to read them.
func TestEnvelopeCarriesRoutingHeader(t *testing.T) {
	msg := protocol.WSMessage{Type: protocol.WSCursor, FromClientID: "c_7", X: 3}
	data, err := json.Marshal(msg)
	if err != nil {
		t.Fatal(err)
	}
	env := EnvelopeOf(msg, data)
	if env.Type != protocol.WSCursor || env.From != "c_7" {
		t.Fatalf("routing header: %+v", env)
	}
	if string(env.Data) != string(data) {
		t.Fatalf("the frame must ride along verbatim: %s", env.Data)
	}
	// It survives a JSON round trip (how the Redis bus crosses processes).
	raw, err := json.Marshal(env)
	if err != nil {
		t.Fatal(err)
	}
	var back Envelope
	if err := json.Unmarshal(raw, &back); err != nil {
		t.Fatal(err)
	}
	if back.Type != env.Type || back.From != env.From || string(back.Data) != string(data) {
		t.Fatalf("round trip: %+v", back)
	}
}

func TestPublishProjectEventIsRoutable(t *testing.T) {
	b := NewInProc()
	defer b.Close()
	ch, cancel := b.Subscribe(ChannelEvents)
	defer cancel()
	PublishProjectEvent(context.Background(), b, protocol.EventDeleted, protocol.ProjectRecord{ID: "p_x_y"})
	select {
	case env := <-ch:
		if env.Type != protocol.WSProjectEv || env.From != "" {
			t.Fatalf("event envelope: %+v", env)
		}
		var msg protocol.WSMessage
		if err := json.Unmarshal(env.Data, &msg); err != nil || msg.Project == nil || msg.Project.ID != "p_x_y" {
			t.Fatalf("event frame: %v %+v", err, msg)
		}
	case <-time.After(time.Second):
		t.Fatal("no event published")
	}
}
