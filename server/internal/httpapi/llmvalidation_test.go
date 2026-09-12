package httpapi

// The per-request bounds re-imposed here rather than trusted: message/image
// counts, text size, the model name, and the system-prompt pin.

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
	"stencil/server/internal/validate"
)

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
		{"too many messages", manyMessages(validate.MaxLLMMessages + 1)},
		{"oversized text", fmt.Sprintf(`{"messages":[{"role":"user","text":%q}]}`, strings.Repeat("a", validate.MaxLLMTextBytes+1))},
		{"oversized text across messages", fmt.Sprintf(`{"messages":[{"role":"user","text":%q},{"role":"assistant","text":%q}]}`,
			strings.Repeat("a", validate.MaxLLMTextBytes/2+1), strings.Repeat("b", validate.MaxLLMTextBytes/2+1))},
		// The model name is forwarded verbatim upstream.
		{"model with a newline", `{"model":"claude\nx","messages":[{"role":"user","text":"x"}]}`},
		{"model with a slash", `{"model":"../../etc","messages":[{"role":"user","text":"x"}]}`},
		{"model too long", fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","text":"x"}]}`, strings.Repeat("m", validate.MaxLLMModelLen+1))},
	}
	for _, c := range cases {
		rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(c.body))
		if rec.Code != http.StatusBadRequest {
			t.Fatalf("%s: want 400, got %d body %s", c.name, rec.Code, rec.Body.String())
		}
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
	if rec := chat(llmEditorPromptHead + strings.Repeat("a", validate.MaxLLMSystemBytes)); rec.Code != http.StatusBadRequest {
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

	rec := do(t, api, http.MethodPost, "/llm/chat", tok, []byte(manyMessages(validate.MaxLLMMessages)))
	if rec.Code != http.StatusOK {
		t.Fatalf("%d messages: want 200, got %d body %s", validate.MaxLLMMessages, rec.Code, rec.Body.String())
	}

	body := fmt.Sprintf(`{"model":%q,"messages":[{"role":"user","text":"hi"}]}`, strings.Repeat("m", validate.MaxLLMModelLen))
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
