package protocol

// The LLM proxy DTOs (llm-contract.md §6.3).

import (
	"encoding/json"
	"testing"
)

// The LLM proxy DTOs are shared with llm-contract.md §6.3.
func TestLlmChatRequestShape(t *testing.T) {
	body := `{"system":"sys","messages":[{"role":"user","text":"hi","images":[{"mediaType":"image/png","data":"AAAA"}]}],"model":"m","maxTokens":100}`
	var req LlmChatRequest
	if err := json.Unmarshal([]byte(body), &req); err != nil {
		t.Fatal(err)
	}
	if req.System != "sys" || req.Model != "m" || req.MaxTokens != 100 || len(req.Messages) != 1 {
		t.Fatalf("decoded %+v", req)
	}
	m := req.Messages[0]
	if m.Role != "user" || m.Text != "hi" || len(m.Images) != 1 ||
		m.Images[0].MediaType != "image/png" || m.Images[0].Data != "AAAA" {
		t.Fatalf("decoded message %+v", m)
	}
	// Messages is required (not omitempty): an empty history still sends the key.
	if !keys(t, LlmChatRequest{})["messages"] {
		t.Error("messages must always be present on the wire")
	}
}

// StopReason passes the provider value through verbatim — clients branch on it
// and must never see it normalised away.
func TestLlmChatResponseAlwaysCarriesStopReason(t *testing.T) {
	for _, reason := range []string{"end_turn", "max_tokens", "refusal", ""} {
		got := keys(t, LlmChatResponse{Model: "m", Text: "t", StopReason: reason})
		if !got["stopReason"] || !got["model"] || !got["text"] {
			t.Errorf("LlmChatResponse(stopReason=%q) emitted %v, want model/text/stopReason", reason, got)
		}
	}
}

// Enabled is false when the server has no API key; false must be on the wire, not
// omitted, or a settings UI would read "absent" as "unknown".
func TestLlmInfoResponseAlwaysCarriesEnabled(t *testing.T) {
	got := keys(t, LlmInfoResponse{})
	if !got["enabled"] || !got["model"] {
		t.Errorf("LlmInfoResponse{} emitted %v, want enabled+model always present", got)
	}
}
