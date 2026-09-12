package httpapi

import (
	"encoding/json"
	"fmt"
	"net/http"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
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
