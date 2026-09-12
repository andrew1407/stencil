package llm

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/protocol"
)

func TestChatRequestTranslation(t *testing.T) {
	d := &mockDoer{status: 200, resp: okResponse}
	c := testClient(d)

	_, err := c.Chat(context.Background(), protocol.LlmChatRequest{
		System: "sys prompt",
		Messages: []protocol.LlmMessage{
			{Role: "user", Text: "look at this", Images: []protocol.LlmImage{
				{MediaType: "image/png", Data: "aGk="},
			}},
			{Role: "assistant", Text: "prior reply"},
		},
	})
	if err != nil {
		t.Fatalf("Chat: %v", err)
	}

	if got := d.req.URL.String(); got != "https://api.example.test/v1/messages" {
		t.Fatalf("URL %q", got)
	}
	if d.req.Method != http.MethodPost {
		t.Fatalf("method %q", d.req.Method)
	}
	for header, want := range map[string]string{
		"x-api-key":         "sk-secret-key",
		"anthropic-version": "2023-06-01",
		"Content-Type":      "application/json",
	} {
		if got := d.req.Header.Get(header); got != want {
			t.Fatalf("header %s = %q, want %q", header, got, want)
		}
	}

	// Exact body translation, including the image source block. No "thinking"
	// or any other field may sneak in.
	want := `{"model":"claude-opus-5","max_tokens":8192,"system":"sys prompt","messages":[` +
		`{"role":"user","content":[{"type":"text","text":"look at this"},` +
		`{"type":"image","source":{"type":"base64","media_type":"image/png","data":"aGk="}}]},` +
		`{"role":"assistant","content":[{"type":"text","text":"prior reply"}]}]}`
	var gotJSON, wantJSON any
	if err := json.Unmarshal(d.body, &gotJSON); err != nil {
		t.Fatalf("request body not JSON: %v", err)
	}
	if err := json.Unmarshal([]byte(want), &wantJSON); err != nil {
		t.Fatal(err)
	}
	gotNorm, _ := json.Marshal(gotJSON)
	wantNorm, _ := json.Marshal(wantJSON)
	if !bytes.Equal(gotNorm, wantNorm) {
		t.Fatalf("request body mismatch:\n got %s\nwant %s", gotNorm, wantNorm)
	}
}

func TestChatStopReasonPassthrough(t *testing.T) {
	for _, reason := range []string{"end_turn", "max_tokens", "refusal"} {
		d := &mockDoer{status: 200, resp: fmt.Sprintf(
			`{"model":"claude-opus-5","stop_reason":%q,"content":[{"type":"text","text":"x"}]}`, reason)}
		resp, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
			Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
		})
		if err != nil {
			t.Fatalf("%s: %v", reason, err)
		}
		if resp.StopReason != reason {
			t.Fatalf("stop reason %q, want %q", resp.StopReason, reason)
		}
	}
}

func TestChatConcatenatesTextBlocks(t *testing.T) {
	d := &mockDoer{status: 200, resp: `{"model":"m","stop_reason":"end_turn","content":[` +
		`{"type":"text","text":"part one "},{"type":"other","text":"skipped"},{"type":"text","text":"part two"}]}`}
	resp, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err != nil {
		t.Fatal(err)
	}
	if resp.Text != "part one part two" {
		t.Fatalf("text %q", resp.Text)
	}
}

func TestChatMaxTokensClampAndModelDefault(t *testing.T) {
	cases := []struct {
		reqTokens  int
		wantTokens int
		reqModel   string
		wantModel  string
	}{
		{0, 8192, "", "claude-opus-5"},             // absent -> server defaults
		{999999, 8192, "", "claude-opus-5"},        // above cap -> clamped
		{100, 100, "custom-model", "custom-model"}, // within cap + explicit model kept
		{-1, 8192, "", "claude-opus-5"},            // nonsense -> server default
	}
	for _, tc := range cases {
		d := &mockDoer{status: 200, resp: okResponse}
		_, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
			Model:     tc.reqModel,
			MaxTokens: tc.reqTokens,
			Messages:  []protocol.LlmMessage{{Role: "user", Text: "hi"}},
		})
		if err != nil {
			t.Fatal(err)
		}
		var sent anthropicRequest
		if err := json.Unmarshal(d.body, &sent); err != nil {
			t.Fatal(err)
		}
		if sent.MaxTokens != tc.wantTokens {
			t.Fatalf("maxTokens %d -> sent %d, want %d", tc.reqTokens, sent.MaxTokens, tc.wantTokens)
		}
		if sent.Model != tc.wantModel {
			t.Fatalf("model %q -> sent %q, want %q", tc.reqModel, sent.Model, tc.wantModel)
		}
	}
}

func TestChatErrorEnvelopeMapping(t *testing.T) {
	d := &mockDoer{status: 400, resp: `{"type":"error","error":{"type":"invalid_request_error","message":"messages: bad"}}`}
	_, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err == nil {
		t.Fatal("expected error")
	}
	msg := err.Error()
	if !strings.Contains(msg, "invalid_request_error") || !strings.Contains(msg, "messages: bad") || !strings.Contains(msg, "400") {
		t.Fatalf("error should carry the envelope type/message/status: %q", msg)
	}
	if strings.Contains(msg, "sk-secret-key") {
		t.Fatalf("error must never include the API key: %q", msg)
	}
}

func TestChatNonJSONErrorBody(t *testing.T) {
	d := &mockDoer{status: 503, resp: "upstream melted"}
	_, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err == nil || !strings.Contains(err.Error(), "503") {
		t.Fatalf("want a status-bearing error, got %v", err)
	}
}

// TestChatOversizedResponseRejected pins the response-body cap: a broken or
// hostile upstream (LLM_BASE_URL is configurable) must not be able to make the
// client buffer unbounded bytes.
func TestChatOversizedResponseRejected(t *testing.T) {
	d := &mockDoer{status: 200, resp: `{"content":[{"type":"text","text":"` +
		strings.Repeat("x", maxResponseBytes) + `"}]}`}
	_, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err == nil || !strings.Contains(err.Error(), "exceeds") {
		t.Fatalf("oversized response should be rejected, got %v", err)
	}
}

func TestChatTransportErrorNeverLeaksKey(t *testing.T) {
	d := &mockDoer{err: fmt.Errorf("dial tcp: connection refused")}
	_, err := testClient(d).Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err == nil || strings.Contains(err.Error(), "sk-secret-key") {
		t.Fatalf("transport error must not leak the key: %v", err)
	}
}

// TestChatAgainstHTTPServer exercises the real *http.Client path built by New.
func TestChatAgainstHTTPServer(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		if req.URL.Path != "/v1/messages" || req.Header.Get("x-api-key") != "k" {
			rw.WriteHeader(http.StatusUnauthorized)
			return
		}
		rw.Header().Set("Content-Type", "application/json")
		_, _ = rw.Write([]byte(okResponse))
	}))
	defer srv.Close()

	c := New(ProviderAnthropic, srv.URL+"/", "k", "claude-opus-5", 8192, time.Second)
	resp, err := c.Chat(context.Background(), protocol.LlmChatRequest{
		Messages: []protocol.LlmMessage{{Role: "user", Text: "hi"}},
	})
	if err != nil {
		t.Fatal(err)
	}
	if resp.Text != "hello" || resp.StopReason != "end_turn" || resp.Model != "claude-opus-5" {
		t.Fatalf("unexpected response %+v", resp)
	}
}
