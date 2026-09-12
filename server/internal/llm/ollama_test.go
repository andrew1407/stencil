package llm

// The Ollama mapping (llm-contract.md §6).

import (
	"context"
	"encoding/json"
	"strings"
	"testing"
)

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
