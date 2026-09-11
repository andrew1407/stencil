package redisbus

import (
	"context"
	"os"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
)

// requireRedis returns a live bus or skips the test when REDIS_URL is unset or
// unreachable, mirroring the self-skipping e2e convention used by mcp/.
func requireRedis(t *testing.T) *Bus {
	t.Helper()
	url := os.Getenv("REDIS_URL")
	if url == "" {
		t.Skip("REDIS_URL not set; skipping Redis integration test")
	}
	b, err := New(context.Background(), url)
	if err != nil {
		t.Skipf("Redis unreachable (%v); skipping", err)
	}
	return b
}

func TestRedisPubSubRoundTrip(t *testing.T) {
	b := requireRedis(t)
	defer b.Close()
	ctx := context.Background()

	ch, cancel := b.Subscribe("test:proj:1")
	defer cancel()
	time.Sleep(100 * time.Millisecond) // let the subscription register

	env := bus.Envelope{Type: protocol.WSEdit, From: "c_1", Data: []byte(`{"type":"edit"}`)}
	if err := b.Publish(ctx, "test:proj:1", env); err != nil {
		t.Fatal(err)
	}
	select {
	case m := <-ch:
		// The envelope crosses the process boundary intact: routing header + frame.
		if m.Type != env.Type || m.From != env.From || string(m.Data) != string(env.Data) {
			t.Fatalf("got %+v", m)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timeout")
	}
}

func TestRedisUnsubscribeClosesChannel(t *testing.T) {
	b := requireRedis(t)
	defer b.Close()
	ch, cancel := b.Subscribe("test:proj:2")
	cancel()
	select {
	case _, open := <-ch:
		if open {
			t.Fatal("expected closed channel after cancel")
		}
	case <-time.After(2 * time.Second):
		t.Fatal("channel not closed after cancel")
	}
}
