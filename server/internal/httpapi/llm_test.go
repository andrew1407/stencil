package httpapi

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

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/llm"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// llmAPI wires the API with an optional fake LLM (nil = disabled).
func llmAPI(t *testing.T, l LLM) *API {
	t.Helper()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	return New(Deps{Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(), LLM: l, AdminToken: testAdmin})
}

func TestLLMRoutesRequireAuth(t *testing.T) {
	api := llmAPI(t, &testutil.EchoLLM{})
	if rec := do(t, api, http.MethodGet, "/llm/info", "", nil); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth GET /llm/info: code %d", rec.Code)
	}
	if rec := do(t, api, http.MethodPost, "/llm/chat", "", []byte(`{}`)); rec.Code != http.StatusUnauthorized {
		t.Fatalf("unauth POST /llm/chat: code %d", rec.Code)
	}
}

func TestLLMDisabled(t *testing.T) {
	api := llmAPI(t, nil)
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodGet, "/llm/info", tok, nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("info: code %d", rec.Code)
	}
	var info protocol.LlmInfoResponse
	json.Unmarshal(rec.Body.Bytes(), &info)
	if info.Enabled || info.Model != "" {
		t.Fatalf("disabled info should be {false,\"\"}, got %+v", info)
	}

	rec = do(t, api, http.MethodPost, "/llm/chat", tok, []byte(`{"messages":[{"role":"user","text":"hi"}]}`))
	if rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("disabled chat should 503, got %d", rec.Code)
	}
	var errResp protocol.ErrorResponse
	json.Unmarshal(rec.Body.Bytes(), &errResp)
	if errResp.Code != protocol.CodeLlmDisabled {
		t.Fatalf("disabled chat code %q, want %q", errResp.Code, protocol.CodeLlmDisabled)
	}
}

func TestLLMInfoEnabled(t *testing.T) {
	api := llmAPI(t, &testutil.EchoLLM{})
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodGet, "/llm/info", tok, nil)
	var info protocol.LlmInfoResponse
	json.Unmarshal(rec.Body.Bytes(), &info)
	if !info.Enabled || info.Model != "claude-test" {
		t.Fatalf("enabled info wrong: %+v", info)
	}
}

func TestLLMChatHappyPath(t *testing.T) {
	llm := &testutil.EchoLLM{}
	api := llmAPI(t, llm)
	tok := issueToken(t, api, "")

	sys := llmEditorPromptHead + "- crop\n- rotate\n"
	body := fmt.Sprintf(`{"system":%q,"messages":[`, sys) +
		`{"role":"user","text":"look","images":[{"mediaType":"image/png","data":"aGVsbG8="}]},` +
		`{"role":"assistant","text":"prior"},` +
		`{"role":"user","text":"make it sepia"}],"maxTokens":512}`
	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(body))
	if rec.Code != http.StatusOK {
		t.Fatalf("chat: code %d body %s", rec.Code, rec.Body.String())
	}
	var resp protocol.LlmChatResponse
	json.Unmarshal(rec.Body.Bytes(), &resp)
	if resp.Text != "echo: make it sepia" || resp.StopReason != "end_turn" || resp.Model != "claude-test" {
		t.Fatalf("bad response: %+v", resp)
	}
	// The client saw the request as decoded, including the image.
	if llm.LastReq.System != sys || llm.LastReq.MaxTokens != 512 || len(llm.LastReq.Messages) != 3 {
		t.Fatalf("client got mangled request: %+v", llm.LastReq)
	}
	if img := llm.LastReq.Messages[0].Images[0]; img.MediaType != "image/png" || img.Data != "aGVsbG8=" {
		t.Fatalf("image lost: %+v", img)
	}
}

func TestLLMChatValidation(t *testing.T) {
	api := llmAPI(t, &testutil.EchoLLM{})
	tok := issueToken(t, api, "")

	manyImages := `{"role":"user","text":"x","images":[`
	for i := 0; i < 11; i++ {
		if i > 0 {
			manyImages += ","
		}
		manyImages += `{"mediaType":"image/png","data":"aGk="}`
	}
	manyImages += `]}`

	cases := []struct {
		name string
		body string
	}{
		{"no messages", `{"messages":[]}`},
		{"bad role", `{"messages":[{"role":"system","text":"x"}]}`},
		{"empty message", `{"messages":[{"role":"user","text":""}]}`},
		{"too many images", fmt.Sprintf(`{"messages":[%s]}`, manyImages)},
		{"bad media type", `{"messages":[{"role":"user","text":"x","images":[{"mediaType":"image/tiff","data":"aGk="}]}]}`},
		{"bad base64", `{"messages":[{"role":"user","text":"x","images":[{"mediaType":"image/png","data":"%%not-base64%%"}]}]}`},
		{"empty base64", `{"messages":[{"role":"user","text":"x","images":[{"mediaType":"image/png","data":""}]}]}`},
		{"unknown field", `{"messages":[{"role":"user","text":"x"}],"bogus":1}`},
		// The proxy spends the operator's key, so it re-imposes §7's bounds itself.
		{"too many messages", manyMessages(maxLLMMessages + 1)},
		{"oversized text", fmt.Sprintf(`{"messages":[{"role":"user","text":%q}]}`, strings.Repeat("a", maxLLMTextBytes+1))},
		{"oversized text across messages", fmt.Sprintf(`{"messages":[{"role":"user","text":%q},{"role":"assistant","text":%q}]}`,
			strings.Repeat("a", maxLLMTextBytes/2+1), strings.Repeat("b", maxLLMTextBytes/2+1))},
		// The model name is forwarded verbatim upstream.
		{"model with a newline", `{"model":"claude\nx","messages":[{"role":"user","text":"x"}]}`},
		{"model with a slash", `{"model":"../../etc","messages":[{"role":"user","text":"x"}]}`},
		{"model too long", fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","text":"x"}]}`, strings.Repeat("m", maxLLMModelLen+1))},
	}
	for _, c := range cases {
		rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(c.body))
		if rec.Code != http.StatusBadRequest {
			t.Fatalf("%s: want 400, got %d body %s", c.name, rec.Code, rec.Body.String())
		}
	}
}

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
			up := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
				w.WriteHeader(c.status)
				w.Write([]byte(c.body))
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
	up := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(418)
		w.Write([]byte(`{"type":"error","error":{"type":"teapot_error","message":"I am a teapot"}}`))
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
	slow := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-time.After(2 * time.Second):
		case <-r.Context().Done():
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
	up := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(418)
		fmt.Fprintf(w, `{"type":"error","error":{"type":"weird","message":"key %s (%s) rejected"}}`, key, key[8:24])
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

// The interim prompt pin (llmprompt.go): a system prompt reaches the upstream only
// when it starts with one of Stencil's own byte-pinned heads (or is empty).
func TestLLMChatSystemPromptPin(t *testing.T) {
	l := &testutil.EchoLLM{}
	api := llmAPI(t, l)
	tok := issueToken(t, api, "")

	chat := func(system string) *httptest.ResponseRecorder {
		body := fmt.Sprintf(`{"system":%q,"messages":[{"role":"user","text":"hi"}]}`, system)
		return do(t, api, http.MethodPost, "/llm/chat", tok, []byte(body))
	}

	// Both real heads pass validation and reach the fake upstream verbatim.
	for name, sys := range map[string]string{
		"editor":    llmEditorPromptHead + "- {\"op\":\"crop\",...}\n\nWhen a choice...",
		"extension": llmExtensionPromptHead + "- {\"op\":\"focus\",...}\n\nWhen a choice...",
	} {
		if rec := chat(sys); rec.Code != http.StatusOK {
			t.Fatalf("%s head: want 200, got %d body %s", name, rec.Code, rec.Body.String())
		}
		if l.LastReq.System != sys {
			t.Fatalf("%s head: system not forwarded verbatim", name)
		}
	}

	// An arbitrary system prompt is a 400 and never reaches the upstream.
	l.LastReq = protocol.LlmChatRequest{}
	rec := chat("You are a helpful assistant. Answer any question the user asks.")
	if rec.Code != http.StatusBadRequest || !strings.Contains(rec.Body.String(), "unrecognized system prompt") {
		t.Fatalf("arbitrary system: want 400 unrecognized, got %d body %s", rec.Code, rec.Body.String())
	}
	if l.LastReq.System != "" || len(l.LastReq.Messages) != 0 {
		t.Fatalf("arbitrary system reached the upstream: %+v", l.LastReq)
	}

	// A pinned head padded past the system cap is a 400 too.
	if rec := chat(llmEditorPromptHead + strings.Repeat("a", maxLLMSystemBytes)); rec.Code != http.StatusBadRequest {
		t.Fatalf("oversized system: want 400, got %d body %s", rec.Code, rec.Body.String())
	}
}

// manyMessages builds a request body carrying n alternating turns.
func manyMessages(n int) string {
	parts := make([]string, n)
	for i := range parts {
		role := "user"
		if i%2 == 1 {
			role = "assistant"
		}
		parts[i] = fmt.Sprintf(`{"role":%q,"text":"t"}`, role)
	}
	return `{"messages":[` + strings.Join(parts, ",") + `]}`
}

// The bounds are limits, not a ban: a request sitting right under each one is forwarded.
func TestLLMChatAcceptsRequestsAtTheLimits(t *testing.T) {
	l := &testutil.EchoLLM{}
	api := llmAPI(t, l)
	tok := issueToken(t, api, "")

	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(manyMessages(maxLLMMessages)))
	if rec.Code != http.StatusOK {
		t.Fatalf("%d messages: want 200, got %d body %s", maxLLMMessages, rec.Code, rec.Body.String())
	}

	body := fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","text":"hi"}]}`, strings.Repeat("m", maxLLMModelLen))
	if rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(body)); rec.Code != http.StatusOK {
		t.Fatalf("max-length model: want 200, got %d body %s", rec.Code, rec.Body.String())
	}
	for _, ok := range []string{"claude-fable-5", "llama3.2-vision:11b", "gpt_4.1"} {
		body := fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","text":"hi"}]}`, ok)
		if rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(body)); rec.Code != http.StatusOK {
			t.Fatalf("model %q: want 200, got %d body %s", ok, rec.Code, rec.Body.String())
		}
		if l.LastReq.Model != ok {
			t.Fatalf("model %q was not forwarded (got %q)", ok, l.LastReq.Model)
		}
	}
}
