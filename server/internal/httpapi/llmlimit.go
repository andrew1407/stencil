package httpapi

// Spend controls for POST /llm/chat. Every accepted turn spends the OPERATOR's
// upstream and a session token is valid for TOKEN_TTL_HOURS, so "authenticated"
// is not a budget. Two caps, both 0 = unlimited: a per-session rate
// (LLM_RATE_PER_MINUTE, spending the shared bucket in internal/ratelimit) and
// the server-wide in-flight cap below (LLM_MAX_IN_FLIGHT), which also bounds
// heap — each call in flight can hold an 8 MiB response plus its images.
// In-process only: a multi-instance deployment limits per instance.

// llmGate bounds concurrent upstream calls. The zero value (nil channel) is
// unlimited.
type llmGate struct{ slots chan struct{} }

func newLLMGate(n int) *llmGate {
	if n <= 0 {
		return &llmGate{}
	}
	return &llmGate{slots: make(chan struct{}, n)}
}

// enter takes a slot without blocking, returning a release func and whether one
// was free. Non-blocking on purpose: queueing behind a full gate would hold the
// client for the whole upstream timeout and answer late anyway.
func (g *llmGate) enter() (func(), bool) {
	if g == nil || g.slots == nil {
		return func() {}, true
	}
	select {
	case g.slots <- struct{}{}:
		return func() { <-g.slots }, true
	default:
		return nil, false
	}
}
