package httpapi

// The /llm/chat spend controls (llmlimit.go). A session token is valid for days
// and every accepted turn spends the operator's upstream, so these are what stop
// one client from draining it. The bucket arithmetic is tested once, in
// internal/ratelimit; these cover the gate and the route wiring.

import (
	"context"
	"encoding/json"
	"net/http"
	"sync"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

func TestGateCapsConcurrencyAndReleases(t *testing.T) {
	g := newLLMGate(2)
	r1, ok1 := g.enter()
	_, ok2 := g.enter()
	if !ok1 || !ok2 {
		t.Fatal("both slots should be available")
	}
	if _, ok := g.enter(); ok {
		t.Fatal("a third concurrent call must be refused, not queued")
	}
	r1() // first call finishes
	r3, ok := g.enter()
	if !ok {
		t.Fatal("the freed slot should be reusable")
	}
	r3()

	// 0 = unlimited, and the release func is always safe to call.
	unl := newLLMGate(0)
	for i := 0; i < 100; i++ {
		rel, ok := unl.enter()
		if !ok {
			t.Fatal("an unlimited gate must never refuse")
		}
		rel()
	}
}

// blockingLLM holds each call until released, so in-flight calls really overlap.
type blockingLLM struct {
	release chan struct{}
	wg      sync.WaitGroup
}

func (b *blockingLLM) Chat(_ context.Context, _ protocol.LlmChatRequest) (protocol.LlmChatResponse, error) {
	b.wg.Done()
	<-b.release
	return protocol.LlmChatResponse{Model: "m", Text: "ok", StopReason: "end_turn"}, nil
}
func (b *blockingLLM) Model() string { return "m" }

func limitedAPI(t *testing.T, l LLM, perMin, inFlight int) *API {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	return New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), LLM: l,
		LLMRatePerMin: perMin, LLMMaxInFlight: inFlight, AdminToken: testAdmin})
}

func TestLLMChatRateLimitReturns429(t *testing.T) {
	api := limitedAPI(t, &testutil.EchoLLM{}, 2, 0)
	tok := issueToken(t, api, "")
	body := []byte(`{"messages":[{"role":"user","text":"hi"}]}`)

	for i := 0; i < 2; i++ {
		if rec := do(t, api, http.MethodPost, "/llm/chat", tok, body); rec.Code != http.StatusOK {
			t.Fatalf("turn %d: code %d", i+1, rec.Code)
		}
	}
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, body)
	if rec.Code != http.StatusTooManyRequests {
		t.Fatalf("over the rate should 429, got %d", rec.Code)
	}
	if rec.Header().Get("Retry-After") == "" {
		t.Fatal("a 429 should say when to come back")
	}
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if errResp.Code != protocol.CodeRateLimited {
		t.Fatalf("code %q, want %q", errResp.Code, protocol.CodeRateLimited)
	}
	// GET /llm/info is not metered — it spends nothing upstream.
	if rec := do(t, api, http.MethodGet, "/llm/info", tok, nil); rec.Code != http.StatusOK {
		t.Fatalf("info should stay reachable while chat is limited, got %d", rec.Code)
	}
}

func TestLLMChatRefusesOverTheInFlightCap(t *testing.T) {
	blocker := &blockingLLM{release: make(chan struct{})}
	api := limitedAPI(t, blocker, 0, 1)
	tok := issueToken(t, api, "")
	body := []byte(`{"messages":[{"role":"user","text":"hi"}]}`)

	blocker.wg.Add(1)
	go func() { do(t, api, http.MethodPost, "/llm/chat", tok, body) }()
	blocker.wg.Wait() // the first call is now inside Chat, holding the only slot

	rec := do(t, api, http.MethodPost, "/llm/chat", tok, body)
	if rec.Code != http.StatusTooManyRequests {
		t.Fatalf("over the in-flight cap should 429, got %d", rec.Code)
	}
	close(blocker.release)
}
