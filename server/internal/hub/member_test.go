package hub

import "testing"

// drain pops one queued frame the way writeLoop does.
func drain(m *member) int {
	data := <-m.out
	m.release(len(data))
	return len(data)
}

// The per-member backlog is bounded by bytes, not by message count: 256 frames
// of up to transport.MaxMessageBytes each would have been 4 GiB.
func TestMemberQueueIsByteBudgeted(t *testing.T) {
	m := newMember("c_1", "peer", nil)

	// A single oversized frame still goes through on an empty queue — the budget
	// bounds a backlog, it does not censor big messages.
	huge := make([]byte, outBudgetBytes+1)
	if !m.enqueue(huge) {
		t.Fatal("an empty queue must accept any one frame")
	}
	if m.enqueue([]byte("tiny")) {
		t.Fatal("a member past the byte budget must drop, not queue")
	}
	if got := drain(m); got != len(huge) {
		t.Fatalf("drained %d bytes", got)
	}
	if !m.enqueue([]byte("tiny")) {
		t.Fatal("draining the backlog must free the budget again")
	}
	drain(m)

	// Half the budget still leaves room for a small frame.
	half := make([]byte, outBudgetBytes/2)
	if !m.enqueue(half) || !m.enqueue([]byte("small")) {
		t.Fatal("a frame that fits inside the budget must queue")
	}
	if m.enqueue(half) {
		t.Fatal("the second half-budget frame must be dropped")
	}
}

// The message-count buffer is still the other bound: many tiny frames cannot
// queue without limit either.
func TestMemberQueueStillBoundsMessageCount(t *testing.T) {
	m := newMember("c_2", "peer", nil)
	accepted := 0
	for i := 0; i < outBuffer*2; i++ {
		if m.enqueue([]byte("x")) {
			accepted++
		}
	}
	if accepted != outBuffer {
		t.Fatalf("accepted %d frames, want the %d-message buffer", accepted, outBuffer)
	}
	if m.queued != outBuffer {
		t.Fatalf("queued bytes drifted from the queue depth: %d", m.queued)
	}
}
