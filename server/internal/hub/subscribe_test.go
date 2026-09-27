package hub

import (
	"context"
	"sync"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// slowBus holds one channel's SUBSCRIBE open, as Redis does while it waits for the ack.
type slowBus struct {
	eventbus.Bus
	slow    string
	entered chan struct{}
	release chan struct{}
	once    sync.Once
}

func (b *slowBus) Subscribe(channel string) (<-chan eventbus.Envelope, func()) {
	if channel == b.slow {
		b.once.Do(func() { close(b.entered) })
		<-b.release
	}
	return b.Bus.Subscribe(channel)
}

// A slow subscribe stalls only its own project: the hub lock is not held across it, so other projects
// join and the REST delete guard still reads counts.
func TestSlowSubscribeDoesNotHoldTheHub(t *testing.T) {
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: "p_t_a"})
	st.Seed(protocol.ProjectRecord{ID: "p_t_b"})
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	bus := &slowBus{Bus: eventbus.NewInProc(), slow: eventbus.ProjectChannel("p_t_a"),
		entered: make(chan struct{}), release: make(chan struct{})}
	h := New(context.Background(), st, bus, st)
	t.Cleanup(h.Close)
	addr := startTCP(t, h)

	slowJoined := make(chan struct{})
	go func() {
		c, err := testutil.DialTCP(addr)
		if err != nil {
			return
		}
		t.Cleanup(func() { c.Close(0, "") })
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a"})
		send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
		readUntil(t, c, protocol.WSWelcome)
		close(slowJoined)
	}()
	<-bus.entered // p_t_a's subscribe is in flight
	joinProject(t, addr, "p_t_b", "B")
	if n := h.ConnectionCount("p_t_a"); n != 1 {
		t.Fatalf("the waiting joiner holds %d refs, want 1", n)
	}
	close(bus.release)
	select {
	case <-slowJoined:
	case <-time.After(3 * time.Second):
		t.Fatal("the joiner behind the slow subscribe never got its welcome")
	}
}
