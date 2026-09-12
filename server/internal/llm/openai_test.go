package llm

// The OpenAI-compatible mapping (llm-contract.md §6): request translation, the
// bearer, the content shape, and finish-reason mapping.

import (
	"context"
	"encoding/json"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

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
