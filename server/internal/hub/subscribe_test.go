package hub

import (
	"context"
	"sync/atomic"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/testutil"
)

// countingBus counts subscriptions, each one a Redis connection on the real bus.
type countingBus struct {
	eventbus.Bus
	subs atomic.Int32
}

func (b *countingBus) Subscribe(channel string) (<-chan eventbus.Envelope, func()) {
	b.subs.Add(1)
	return b.Bus.Subscribe(channel)
}

// Every connection listens through the hub's one subscription, however many there are.
func TestFeedConnectionsShareOneBusSubscription(t *testing.T) {
	st := testutil.NewMemStore()
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	bus := &countingBus{Bus: eventbus.NewInProc()}
	h := New(context.Background(), bus, st)
	t.Cleanup(h.Close)
	addr := startTCP(t, h)
	for range 5 {
		joinFeed(t, h, addr)
	}
	if n := bus.subs.Load(); n != 1 {
		t.Fatalf("five feeds took %d bus subscriptions, want 1", n)
	}
}

// A listener that stops reading drops past its buffer; delivery to the others never waits on it.
func TestASlowListenerDropsWithoutStallingTheOthers(t *testing.T) {
	f := newFeed(2)
	slow, unsubSlow := f.listen()
	defer unsubSlow()
	fast, unsubFast := f.listen()
	defer unsubFast()
	for i := range 10 {
		f.deliver([]byte{byte(i)})
		if got := <-fast; got[0] != byte(i) {
			t.Fatalf("the reading listener got %v at %d", got, i)
		}
	}
	if len(slow) != 2 {
		t.Fatalf("the stalled listener holds %d frames, want its buffer of 2", len(slow))
	}
}

// When the bus subscription ends, every listener ends, and a later one is closed from the start.
func TestAClosedFeedEndsItsListeners(t *testing.T) {
	f := newFeed(1)
	ch, unsub := f.listen()
	f.close()
	if _, ok := <-ch; ok {
		t.Fatal("a listener survived the feed's close")
	}
	unsub() // safe after close
	late, _ := f.listen()
	if _, ok := <-late; ok {
		t.Fatal("a listener on a closed feed was left open")
	}
}

// A hub whose subscription closes hangs up its feeds rather than leaving them silent.
func TestFeedConnectionReturnsWhenTheBusEnds(t *testing.T) {
	h := newTestHub(t)
	c := joinFeed(t, h, startTCP(t, h))
	h.feed.close()
	expectClosed(t, c, "after the bus ended")
}
