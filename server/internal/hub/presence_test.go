package hub

import (
	"maps"
	"testing"
	"time"
)

// drainChanged empties a pending LiveChanged signal so the next wait sees only what follows.
func drainChanged(h *Hub) {
	select {
	case <-h.LiveChanged():
	default:
	}
}

func expectChanged(t *testing.T, h *Hub, what string) {
	t.Helper()
	select {
	case <-h.LiveChanged():
	case <-time.After(2 * time.Second):
		t.Fatalf("no LiveChanged signal after %s", what)
	}
}

// LiveCounts is what the presence heartbeat publishes: every live project with its member count,
// moving as clients join and leave, and LiveChanged fires on each move.
func TestLiveCountsFollowJoinsAndLeaves(t *testing.T) {
	h := newTestHub(t)
	addr := startTCP(t, h)
	if got := h.LiveCounts(); len(got) != 0 {
		t.Fatalf("no sessions should count nothing, got %v", got)
	}

	drainChanged(h)
	a := joinProject(t, addr, "p_t_a", "A")
	expectChanged(t, h, "a join")
	b := joinProject(t, addr, "p_t_a", "B")
	waitFor(t, func() bool { return maps.Equal(h.LiveCounts(), map[string]int{"p_t_a": 2}) })

	drainChanged(h)
	a.Close(0, "bye")
	expectChanged(t, h, "a leave")
	waitFor(t, func() bool { return maps.Equal(h.LiveCounts(), map[string]int{"p_t_a": 1}) })
	b.Close(0, "bye")
	waitFor(t, func() bool { return len(h.LiveCounts()) == 0 })
}

// Signals coalesce: a burst of changes leaves one pending signal, never a blocked join.
func TestLiveChangedCoalesces(t *testing.T) {
	h := newTestHub(t)
	for range 5 {
		h.signalChanged()
	}
	<-h.LiveChanged()
	select {
	case <-h.LiveChanged():
		t.Fatal("a burst left more than one pending signal")
	default:
	}
}
