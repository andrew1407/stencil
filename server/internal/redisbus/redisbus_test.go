package redisbus

import (
	"context"
	"os"
	"testing"
	"time"
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

	if err := b.Publish(ctx, "test:proj:1", []byte("edit-op")); err != nil {
		t.Fatal(err)
	}
	select {
	case m := <-ch:
		if string(m) != "edit-op" {
			t.Fatalf("got %q", m)
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
