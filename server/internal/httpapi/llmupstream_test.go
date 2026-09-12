package httpapi

// What a failing upstream turns into for the client: a sanitized reason it can
// act on, never the key, and nothing at all once the caller has gone away.

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/llm"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// An internal fault (a bug in this server, not an upstream condition) keeps the
// package's fixed message: there is nothing the user could act on.
func TestLLMChatInternalErrorKeepsTheGenericMessage(t *testing.T) {
	api := llmAPI(t, &testutil.EchoLLM{Err: errors.New("json: unsupported value NaN")})
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	if rec.Code != http.StatusBadGateway {
		t.Fatalf("internal error should 502, got %d", rec.Code)
	}
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if errResp.Code != protocol.CodeInternal || errResp.Message != "LLM request failed" {
		t.Fatalf("internal fault should stay generic, got %+v", errResp)
	}
	if strings.Contains(rec.Body.String(), "NaN") {
		t.Fatalf("internal detail must not leak: %s", rec.Body.String())
	}
}

// An upstream condition the user can fix answers 502 llmUpstream and SAYS WHY —
// exactly once: the bare reason, no status and no upstream prose (llm-contract.md
// §6.3) — end to end through the real llm client against a stub upstream, one
// shape per provider.
func TestLLMChatUpstreamFailuresSayWhy(t *testing.T) {
	cases := []struct {
		name, provider string
		status         int
		body           string
		want           string // the whole message
		absent         string // upstream prose that must not be restated
	}{
		{"anthropic out of credits", llm.ProviderAnthropic, 400,
			`{"type":"error","error":{"type":"invalid_request_error","message":"Your credit balance is too low to access the Anthropic API. Please go to Plans & Billing to upgrade or purchase credits."}}`,
			"the LLM provider is out of credits or has no active billing", "Plans & Billing"},
		{"anthropic revoked key", llm.ProviderAnthropic, 401,
			`{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}`,
			"the LLM provider rejected the API key", "x-api-key"},
		{"openai unknown model", llm.ProviderOpenAI, 404,
			`{"error":{"message":"The model 'gpt-nope' does not exist","code":"model_not_found"}}`,
			"the LLM provider does not have the requested model", "gpt-nope"},
		{"openai upstream rate limit", llm.ProviderOpenAI, 429,
			`{"error":{"message":"Rate limit reached","code":"rate_limit_exceeded"}}`,
			"the LLM provider is rate-limiting this server", "Rate limit reached"},
		{"ollama model not pulled", llm.ProviderOllama, 404,
			`{"error":"model 'llava' not found, try pulling it first"}`,
			"the LLM provider does not have the requested model", "try pulling"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			up := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
				rw.WriteHeader(c.status)
				rw.Write([]byte(c.body))
			}))
			defer up.Close()
			api := llmAPI(t, llm.New(c.provider, up.URL, "sk-secret-key-value", "m", 1024, 5*time.Second))
			tok := issueToken(t, api, "")
			rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
			if rec.Code != http.StatusBadGateway {
				t.Fatalf("upstream failure should 502, got %d", rec.Code)
			}
			var errResp protocol.ErrorResponse
			json.Unmarshal(rec.Body.Bytes(), &errResp)
			if errResp.Code != protocol.CodeLlmUpstream {
				t.Fatalf("code %q, want %q", errResp.Code, protocol.CodeLlmUpstream)
			}
			if errResp.Message != c.want {
				t.Fatalf("message %q, want exactly %q", errResp.Message, c.want)
			}
			// Neither the status nor the upstream's own prose is restated.
			if strings.Contains(errResp.Message, "HTTP") || strings.Contains(errResp.Message, c.absent) {
				t.Fatalf("message %q should be the reason alone", errResp.Message)
			}
			// The stub's URL is this server's business, not the client's.
			if strings.Contains(rec.Body.String(), up.URL) {
				t.Fatalf("upstream URL leaked: %s", rec.Body.String())
			}
		})
	}
}

// A shape nothing matches is the one case that still forwards the upstream's own
// (bounded, sanitized) text plus its status — there is no reason to say instead.
func TestLLMChatUnknownUpstreamShapeKeepsItsText(t *testing.T) {
	up := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(418)
		rw.Write([]byte(`{"type":"error","error":{"type":"teapot_error","message":"I am a teapot"}}`))
	}))
	defer up.Close()

	api := llmAPI(t, llm.New(llm.ProviderAnthropic, up.URL, "sk-secret-key-value", "m", 1024, 5*time.Second))
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if rec.Code != http.StatusBadGateway || errResp.Code != protocol.CodeLlmUpstream ||
		!strings.Contains(errResp.Message, "I am a teapot") || !strings.Contains(errResp.Message, "HTTP 418") {
		t.Fatalf("unknown shape: %d %+v", rec.Code, errResp)
	}
}

// An unreachable upstream reads as unreachable, not as "LLM request failed".
func TestLLMChatUnreachableUpstream(t *testing.T) {
	dead := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {}))
	addr := dead.URL
	dead.Close() // nothing listens there now

	api := llmAPI(t, llm.New(llm.ProviderAnthropic, addr, "sk-secret-key-value", "m", 1024, 5*time.Second))
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if rec.Code != http.StatusBadGateway || errResp.Code != protocol.CodeLlmUpstream ||
		!strings.Contains(errResp.Message, "unreachable") {
		t.Fatalf("dial failure: %d %+v", rec.Code, errResp)
	}
	if strings.Contains(errResp.Message, addr) {
		t.Fatalf("dial detail leaked the endpoint: %q", errResp.Message)
	}
}

// A slow upstream is a timeout, and the client is told so.
func TestLLMChatUpstreamTimeout(t *testing.T) {
	slow := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		select {
		case <-time.After(2 * time.Second):
		case <-req.Context().Done():
		}
	}))
	defer slow.Close()

	api := llmAPI(t, llm.New(llm.ProviderAnthropic, slow.URL, "", "m", 1024, 50*time.Millisecond))
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if rec.Code != http.StatusBadGateway || errResp.Code != protocol.CodeLlmUpstream ||
		!strings.Contains(errResp.Message, "did not respond in time") {
		t.Fatalf("timeout: %d %+v", rec.Code, errResp)
	}
}

// Whatever an upstream echoes back, the key never rides out to a client.
func TestLLMChatNeverLeaksTheKeyFromAnUpstreamError(t *testing.T) {
	const key = "sk-ant-api03-AbCdEfGhIjKlMnOpQrStUvWxYz0123456789"
	up := httptest.NewServer(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(418)
		fmt.Fprintf(rw, `{"type":"error","error":{"type":"weird","message":"key %s (%s) rejected"}}`, key, key[8:24])
	}))
	defer up.Close()

	api := llmAPI(t, llm.New(llm.ProviderAnthropic, up.URL, key, "m", 1024, 5*time.Second))
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	body := rec.Body.String()
	if rec.Code != http.StatusBadGateway {
		t.Fatalf("code %d", rec.Code)
	}
	for _, leak := range []string{key, key[8:24], key[:12], "sk-ant"} {
		if strings.Contains(body, leak) {
			t.Fatalf("key material %q reached the client: %s", leak, body)
		}
	}
}

// A client that hangs up mid-call is still not an upstream failure: nothing is
// written, and no 502 is manufactured for a connection nobody is reading.
func TestLLMChatClientCancellationWritesNothing(t *testing.T) {
	api := llmAPI(t, &testutil.EchoLLM{Err: context.Canceled})
	tok := issueToken(t, api, "")
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	r := httptest.NewRequest(http.MethodPost, "/llm/chat",
		strings.NewReader(`{"messages":[{"role":"user","text":"hi"}]}`)).WithContext(ctx)
	r.Header.Set("Authorization", "Bearer "+tok)
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, r)
	if rec.Body.Len() != 0 {
		t.Fatalf("a cancelled client should get no body, got %s", rec.Body.String())
	}
}
