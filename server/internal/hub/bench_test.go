package hub

// Opt-in: `go test -bench . ./internal/hub/` (CI never passes -bench). Baselines
// are in server/README.md#benchmarks.

import (
	"encoding/json"
	"fmt"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
)

// benchSession builds a session with n drained members, mimicking n peers in one
// project. Each member's writer is a goroutine that discards, so the benchmark
// measures fan-out and not a socket.
func benchSession(b *testing.B, n int) (*session, func()) {
	b.Helper()
	s := &session{id: "p_b_a", members: map[string]*member{}}
	stop := make(chan struct{})
	for i := 0; i < n; i++ {
		m := newMember(fmt.Sprintf("c%d", i), "peer", nil)
		s.members[m.clientID] = m
		go func(m *member) {
			for {
				select {
				case data := <-m.out:
					m.release(len(data))
				case <-stop:
					return
				}
			}
		}(m)
	}
	return s, func() { close(stop) }
}

// BenchmarkSessionFanout measures one bus delivery reaching every local member.
// The envelope carries type and sender, so fanout must not parse the frame.
func BenchmarkSessionFanout(b *testing.B) {
	msg := protocol.WSMessage{Type: protocol.WSCursor, FromClientID: "c0", X: 120, Y: 240}
	data, err := json.Marshal(msg)
	if err != nil {
		b.Fatal(err)
	}
	env := bus.EnvelopeOf(msg, data)
	for _, peers := range []int{1, 10, 50} {
		b.Run(fmt.Sprintf("peers=%d", peers), func(b *testing.B) {
			s, stop := benchSession(b, peers)
			defer stop()
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				s.fanout(env)
			}
		})
	}
}
