// The non-Anthropic upstreams (llm-contract.md §6.1/§6.2) and the provider
// dispatch: each provider's wire shape, the reply/stopReason mapping back onto
// protocol.LlmChatResponse, and error surfacing. Same mock-Doer seam as
// anthropic_test.go — no network, so the exact bytes are asserted.
package llm

import (
	"context"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

func providerClient(provider, key string, d Doer) *Client {
	return &Client{provider: provider, base: "http://up.example.test", key: key,
		model: "server-default", maxTokens: 8192, http: d}
}

var turnWithImage = protocol.LlmChatRequest{
	System: "sys",
	Messages: []protocol.LlmMessage{{
		Role: "user", Text: "hello",
		Images: []protocol.LlmImage{{MediaType: "image/png", Data: "AAAA"}},
	}},
}

// ── dispatch ──

func TestNewDefaultsToAnthropicAndReportsProvider(t *testing.T) {
	if got := New("", "http://x", "k", "m", 10, 0).Provider(); got != ProviderAnthropic {
		t.Fatalf("empty provider should default to anthropic, got %q", got)
	}
	if got := New(ProviderOllama, "http://x", "", "m", 10, 0).Provider(); got != ProviderOllama {
		t.Fatalf("provider not retained: %q", got)
	}
}

func TestKnownProviderAndDefaults(t *testing.T) {
	for _, p := range []string{"", ProviderAnthropic, ProviderOllama, ProviderOpenAI} {
		if !KnownProvider(p) {
			t.Fatalf("%q should be known", p)
		}
	}
	if KnownProvider("gemini") {
		t.Fatal("unknown providers must be rejected so the proxy stays disabled")
	}
	// The §5 defaults table, mirrored from the clients.
	if got := DefaultBaseURL(ProviderOllama); got != "http://localhost:11434" {
		t.Fatalf("ollama default: %q", got)
	}
	if got := DefaultBaseURL(ProviderOpenAI); got != "http://localhost:1234/v1" {
		t.Fatalf("openai-compat default: %q", got)
	}
	if got := DefaultBaseURL(""); got != "https://api.anthropic.com" {
		t.Fatalf("anthropic default: %q", got)
	}
	// Only Anthropic requires a key — local servers need none.
	if !NeedsKey("") || !NeedsKey(ProviderAnthropic) {
		t.Fatal("anthropic must require a key")
	}
	if NeedsKey(ProviderOllama) || NeedsKey(ProviderOpenAI) {
		t.Fatal("local providers must not require a key")
	}
}

// A key named for one vendor must not follow a provider switch to another host: ANTHROPIC_API_KEY is
// Anthropic-only, LLM_API_KEY is what a local or third-party upstream uses.
func TestResolveKeyKeepsTheAnthropicKeyAwayFromOtherProviders(t *testing.T) {
	for _, p := range []string{"", ProviderAnthropic} {
		if got := ResolveKey(p, "", "sk-ant"); got != "sk-ant" {
			t.Fatalf("anthropic (%q) should use ANTHROPIC_API_KEY, got %q", p, got)
		}
	}
	for _, p := range []string{ProviderOllama, ProviderOpenAI} {
		if got := ResolveKey(p, "", "sk-ant"); got != "" {
			t.Fatalf("%s must NOT receive the Anthropic key, got %q", p, got)
		}
		if got := ResolveKey(p, "sk-local", "sk-ant"); got != "sk-local" {
			t.Fatalf("%s should use LLM_API_KEY, got %q", p, got)
		}
	}
	// LLM_API_KEY wins for Anthropic too — one var can configure every provider.
	if got := ResolveKey(ProviderAnthropic, "sk-new", "sk-ant"); got != "sk-new" {
		t.Fatalf("LLM_API_KEY should take precedence, got %q", got)
	}
}

// ── ollama (§6.1) ──

// ── openai-compat (§6.2) ──

// ── shared guarantees ──

func TestEveryProviderRejectsOversizedResponses(t *testing.T) {
	// The heap guard is in the shared reader, so it must hold for all mappings.
	huge := strings.Repeat("x", maxResponseBytes+1)
	for _, p := range []string{ProviderOllama, ProviderOpenAI} {
		mock := &mockDoer{status: 200, resp: huge}
		if _, err := providerClient(p, "", mock).Chat(context.Background(), turnWithImage); err == nil ||
			!strings.Contains(err.Error(), "exceeds") {
			t.Fatalf("%s: oversized body should be rejected, got %v", p, err)
		}
	}
}

func TestNonAnthropicProvidersNeverSendAnthropicHeaders(t *testing.T) {
	for _, p := range []string{ProviderOllama, ProviderOpenAI} {
		mock := &mockDoer{status: 200, resp: `{"choices":[{"message":{"content":"x"}}],"message":{"content":"x"}}`}
		if _, err := providerClient(p, "sk-secret", mock).Chat(context.Background(), turnWithImage); err != nil {
			t.Fatalf("%s: %v", p, err)
		}
		if mock.req.Header.Get("x-api-key") != "" || mock.req.Header.Get("anthropic-version") != "" {
			t.Fatalf("%s must not send Anthropic headers: %v", p, mock.req.Header)
		}
	}
}
