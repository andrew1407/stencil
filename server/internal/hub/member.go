package hub

import (
	"context"
	"sync"

	"stencil/server/internal/transport"
)

// outBudgetBytes bounds one member's queued backlog in bytes, not messages: a frame may be
// transport.MaxMessageBytes (16 MiB). A message still always fits on an empty queue.
const outBudgetBytes = 8 << 20

// member is one connected client within a session. The run-loop pushes outbound frames onto out; a
// per-member writeLoop drains it to the connection so a slow peer never blocks the run-loop.
type member struct {
	clientID string
	name     string
	conn     transport.Conn
	out      chan []byte

	mu     sync.Mutex
	queued int // bytes sitting in out, guarded by mu
}

func newMember(clientID, name string, conn transport.Conn) *member {
	return &member{clientID: clientID, name: name, conn: conn, out: make(chan []byte, outBuffer)}
}

// enqueue hands data to the member's writer without blocking the run-loop. It drops when the member is
// behind by more than the byte budget; clients reconcile by version on resubscribe.
func (m *member) enqueue(data []byte) bool {
	m.mu.Lock()
	if m.queued > 0 && m.queued+len(data) > outBudgetBytes {
		m.mu.Unlock()
		return false
	}
	m.queued += len(data)
	m.mu.Unlock()
	select {
	case m.out <- data:
		return true
	default: // slow consumer; drop
		m.release(len(data))
		return false
	}
}

// release gives queued bytes back once they leave the queue.
func (m *member) release(n int) {
	m.mu.Lock()
	m.queued -= n
	m.mu.Unlock()
}

func (m *member) writeLoop(ctx context.Context, done chan struct{}) {
	defer close(done)
	for data := range m.out {
		m.release(len(data))
		if err := m.conn.Write(ctx, data); err != nil {
			// Drain remaining sends without writing so the run-loop's close of
			// out is observed and this goroutine exits.
			for range m.out {
			}
			return
		}
	}
}
