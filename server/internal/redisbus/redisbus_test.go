package redisbus

import (
	"context"
	"os"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// requireRedis returns a live bus, skips when REDIS_URL is unset, and fails when it is set
// but unreachable, so a broken service container is never a green skip.
func requireRedis(t *testing.T) eventbus.Bus {
	t.Helper()
	url := os.Getenv("REDIS_URL")
	if url == "" {
		t.Skip("REDIS_URL not set; skipping Redis integration test")
	}
	b, err := New(context.Background(), url)
	if err != nil {
		t.Fatalf("REDIS_URL is set but Redis is unreachable: %v", err)
	}
	return b
}

func TestRedisPubSubRoundTrip(t *testing.T) {
	b := requireRedis(t)
	defer b.Close()
	ctx := context.Background()

	ch, cancel := b.Subscribe("test:proj:1")
	defer cancel()

	env := eventbus.Envelope{Type: protocol.WSProjectEv, Data: []byte(`{"type":"project-event"}`)}
	if err := b.Publish(ctx, "test:proj:1", env); err != nil {
		t.Fatal(err)
	}
	select {
	case m := <-ch:
		// The envelope crosses the process boundary intact: routing header + frame.
		if m.Type != env.Type || string(m.Data) != string(env.Data) {
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
