package llm

// The non-Anthropic upstreams (llm-contract.md §6.1/§6.2) and the provider
// dispatch: each provider's wire shape, the reply/stopReason mapping back onto
// protocol.LlmChatResponse, and error surfacing. Same mock-Doer seam as
// anthropic_test.go — no network, so the exact bytes are asserted.

import (
	"context"
	"encoding/json"
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

// A key named for one vendor must not follow a provider switch to another host:
// ANTHROPIC_API_KEY is Anthropic-only, LLM_API_KEY is what a local/third-party
// upstream uses.
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

func TestOllamaRequestTranslation(t *testing.T) {
	mock := &mockDoer{status: 200, resp: `{"model":"llama3","message":{"role":"assistant","content":"hi there"}}`}
	c := providerClient(ProviderOllama, "", mock)

	got, err := c.Chat(context.Background(), turnWithImage)
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if mock.req.URL.String() != "http://up.example.test/api/chat" {
		t.Fatalf("ollama path: %s", mock.req.URL.String())
	}
	// No key configured → no Authorization header invented for a local server.
	if mock.req.Header.Get("Authorization") != "" {
		t.Fatal("ollama must not send Authorization")
	}
	var sent ollamaRequest
	if err := json.Unmarshal(mock.body, &sent); err != nil {
		t.Fatalf("request JSON: %v", err)
	}
	if sent.Stream {
		t.Fatal("v1 is non-streaming")
	}
	if sent.Model != "server-default" {
		t.Fatalf("model default not applied: %q", sent.Model)
	}
	// The system prompt rides as a leading system MESSAGE (§6.1), and images are
	// bare base64 strings — not data: URLs.
	if len(sent.Messages) != 2 || sent.Messages[0].Role != "system" || sent.Messages[0].Content != "sys" {
		t.Fatalf("system message: %+v", sent.Messages)
	}
	if sent.Messages[1].Content != "hello" || len(sent.Messages[1].Images) != 1 ||
		sent.Messages[1].Images[0] != "AAAA" {
		t.Fatalf("user message: %+v", sent.Messages[1])
	}
	if got.Text != "hi there" || got.Model != "llama3" || got.StopReason != "end_turn" {
		t.Fatalf("response mapping: %+v", got)
	}
}

func TestOllamaErrorSurfacesUpstreamMessage(t *testing.T) {
	mock := &mockDoer{status: 404, resp: `{"error":"model 'nope' not found, try pulling it first"}`}
	_, err := providerClient(ProviderOllama, "", mock).Chat(context.Background(), turnWithImage)
	if err == nil || !strings.Contains(err.Error(), "not found, try pulling it first") {
		t.Fatalf("upstream message should surface verbatim, got %v", err)
	}
}

// ── openai-compat (§6.2) ──

func TestOpenAIRequestTranslationAndBearer(t *testing.T) {
	mock := &mockDoer{status: 200,
		resp: `{"model":"gpt-x","choices":[{"message":{"content":"ok"},"finish_reason":"stop"}]}`}
	c := providerClient(ProviderOpenAI, "sk-local", mock)

	got, err := c.Chat(context.Background(), turnWithImage)
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if mock.req.URL.String() != "http://up.example.test/chat/completions" {
		t.Fatalf("openai path: %s", mock.req.URL.String())
	}
	if mock.req.Header.Get("Authorization") != "Bearer sk-local" {
		t.Fatalf("bearer: %q", mock.req.Header.Get("Authorization"))
	}
	// Images ride as image_url parts carrying a data: URL (§6.2).
	var sent struct {
		Messages []struct {
			Role    string          `json:"role"`
			Content json.RawMessage `json:"content"`
		} `json:"messages"`
	}
	if err := json.Unmarshal(mock.body, &sent); err != nil {
		t.Fatalf("request JSON: %v", err)
	}
	if len(sent.Messages) != 2 || sent.Messages[0].Role != "system" {
		t.Fatalf("system message missing: %+v", sent.Messages)
	}
	if !strings.Contains(string(sent.Messages[1].Content), `"data:image/png;base64,AAAA"`) {
		t.Fatalf("image part: %s", sent.Messages[1].Content)
	}
	if got.Text != "ok" || got.Model != "gpt-x" || got.StopReason != "end_turn" {
		t.Fatalf("response mapping: %+v", got)
	}
}

func TestOpenAIKeyOptionalAndTextOnlyContentIsAString(t *testing.T) {
	mock := &mockDoer{status: 200, resp: `{"choices":[{"message":{"content":"hi"}}]}`}
	c := providerClient(ProviderOpenAI, "", mock) // LM Studio / llama.cpp: no key
	got, err := c.Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "plain"}}})
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if mock.req.Header.Get("Authorization") != "" {
		t.Fatal("no key configured → no Authorization header")
	}
	var sent struct {
		Messages []struct {
			Content json.RawMessage `json:"content"`
		} `json:"messages"`
	}
	_ = json.Unmarshal(mock.body, &sent)
	if len(sent.Messages) != 1 || string(sent.Messages[0].Content) != `"plain"` {
		t.Fatalf("text-only content must be a bare string, got %s", sent.Messages[0].Content)
	}
	// Model falls back to the server default when the upstream echoes none.
	if got.Model != "server-default" {
		t.Fatalf("model fallback: %q", got.Model)
	}
}

func TestOpenAIFinishReasonMapsOntoContractStopReasons(t *testing.T) {
	// §6.3's vocabulary is what clients act on — every provider must speak it.
	for _, tc := range []struct{ finish, want string }{
		{"stop", "end_turn"},
		{"length", "max_tokens"},
		{"content_filter", "refusal"},
	} {
		mock := &mockDoer{status: 200,
			resp: `{"choices":[{"message":{"content":"x"},"finish_reason":"` + tc.finish + `"}]}`}
		got, err := providerClient(ProviderOpenAI, "", mock).Chat(context.Background(), turnWithImage)
		if err != nil {
			t.Fatalf("%s: %v", tc.finish, err)
		}
		if got.StopReason != tc.want {
			t.Fatalf("finish_reason %q → stopReason %q, want %q", tc.finish, got.StopReason, tc.want)
		}
	}
}

func TestOpenAIErrorSurfacesUpstreamMessage(t *testing.T) {
	mock := &mockDoer{status: 400, resp: `{"error":{"message":"invalid model"}}`}
	_, err := providerClient(ProviderOpenAI, "", mock).Chat(context.Background(), turnWithImage)
	if err == nil || !strings.Contains(err.Error(), "invalid model") {
		t.Fatalf("upstream message should surface, got %v", err)
	}
}

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
