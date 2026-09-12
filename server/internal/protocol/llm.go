package protocol

// ----- LLM proxy DTOs (llm-contract.md §6.3) -----

// LlmImage is one base64-encoded image attachment inside a chat turn.
type LlmImage struct {
	MediaType string `json:"mediaType"` // image/png, image/jpeg, image/webp, image/gif
	Data      string `json:"data"`      // base64 payload (no data: prefix)
}

// LlmMessage is one chat turn replayed by the client (history is client-side).
type LlmMessage struct {
	Role   string     `json:"role"` // "user" | "assistant"
	Text   string     `json:"text"`
	Images []LlmImage `json:"images,omitempty"`
}

// LlmChatRequest is the body of POST /llm/chat.
type LlmChatRequest struct {
	System    string       `json:"system,omitempty"`
	Messages  []LlmMessage `json:"messages"`
	Model     string       `json:"model,omitempty"`     // empty = server default
	MaxTokens int          `json:"maxTokens,omitempty"` // clamped server-side
}

// LlmChatResponse is returned by POST /llm/chat. StopReason passes through the
// provider's value: "end_turn" (normal), "max_tokens" (truncated — clients show
// a note and never parse a plan from it), "refusal" (shown as a chat error).
type LlmChatResponse struct {
	Model      string `json:"model"`
	Text       string `json:"text"`
	StopReason string `json:"stopReason"`
}

// LlmInfoResponse is returned by GET /llm/info so settings UIs can render
// "via server X (model)". Enabled is false when the server has no API key.
type LlmInfoResponse struct {
	Enabled bool   `json:"enabled"`
	Model   string `json:"model"`
}
