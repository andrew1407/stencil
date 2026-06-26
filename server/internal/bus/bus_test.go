package bus

import (
	"context"
	"testing"
	"time"
)

func recv(t *testing.T, ch <-chan []byte) []byte {
	t.Helper()
	select {
	case m := <-ch:
		return m
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for message")
		return nil
	}
}

func TestInProcFanout(t *testing.T) {
	b := NewInProc()
	defer b.Close()
	ctx := context.Background()

	c1, cancel1 := b.Subscribe(ctx, "proj:x")
	c2, cancel2 := b.Subscribe(ctx, "proj:x")
	defer cancel1()
	defer cancel2()

	if err := b.Publish(ctx, "proj:x", []byte("hello")); err != nil {
		t.Fatal(err)
	}
	if string(recv(t, c1)) != "hello" || string(recv(t, c2)) != "hello" {
		t.Fatal("both subscribers should receive")
	}
}

func TestInProcChannelIsolation(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	cx, cancelx := b.Subscribe(ctx, "proj:x")
	cy, cancely := b.Subscribe(ctx, "proj:y")
	defer cancelx()
	defer cancely()

	b.Publish(ctx, "proj:x", []byte("only-x"))
	if string(recv(t, cx)) != "only-x" {
		t.Fatal("x subscriber missed message")
	}
	select {
	case m := <-cy:
		t.Fatalf("y subscriber got cross-channel message %q", m)
	case <-time.After(50 * time.Millisecond):
	}
}

func TestInProcUnsubscribeCloses(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	ch, cancel := b.Subscribe(ctx, "c")
	cancel()
	if _, open := <-ch; open {
		t.Fatal("channel should be closed after unsubscribe")
	}
	cancel() // idempotent
	// Publishing to a now-empty channel must not panic.
	if err := b.Publish(ctx, "c", []byte("x")); err != nil {
		t.Fatal(err)
	}
}

func TestInProcSlowSubscriberDropsNotBlocks(t *testing.T) {
	b := NewInProc()
	ctx := context.Background()
	_, cancel := b.Subscribe(ctx, "c") // never drained
	defer cancel()
	// Far more than subBuffer; must not block.
	done := make(chan struct{})
	go func() {
		for i := 0; i < subBuffer*4; i++ {
			b.Publish(ctx, "c", []byte("x"))
		}
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("publish blocked on slow subscriber")
	}
}
