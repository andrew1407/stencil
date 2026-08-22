package llm

// Non-Anthropic upstreams for the proxy (llm-contract.md §6.1/§6.2): the
// same three provider mappings the clients speak, behind the same
// bearer-authenticated /llm/chat route. protocol.LlmChatRequest/Response is
// canonical either way — only the wire shape differs (cf. browser llmClient.js).

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"

	"stencil/server/internal/protocol"
)

// Provider identifiers (the §5 values the clients use, minus the client-only
// "stencil-server" — from this process's side that IS the upstream choice).
const (
	ProviderAnthropic = "anthropic"
	ProviderOllama    = "ollama"
	ProviderOpenAI    = "openai-compat"

	ollamaChatPath = "/api/chat"
	openAIChatPath = "/chat/completions"
)

// KnownProvider reports whether p is one this process can proxy to. Empty
// counts as Anthropic (the historical default).
func KnownProvider(p string) bool {
	switch p {
	case "", ProviderAnthropic, ProviderOllama, ProviderOpenAI:
		return true
	}
	return false
}

// DefaultBaseURL is the per-provider endpoint used when LLM_BASE_URL is unset —
// the same defaults table every client ships (contract §5).
func DefaultBaseURL(provider string) string {
	switch provider {
	case ProviderOllama:
		return "http://localhost:11434"
	case ProviderOpenAI:
		return "http://localhost:1234/v1"
	default:
		return "https://api.anthropic.com"
	}
}

// NeedsKey reports whether a provider requires an API key to be usable. Local
// servers (Ollama, LM Studio, llama.cpp) need none; Anthropic always does.
func NeedsKey(provider string) bool {
	return provider == "" || provider == ProviderAnthropic
}

// ResolveKey picks the credential for provider. LLM_API_KEY (apiKey) is
// provider-agnostic; ANTHROPIC_API_KEY (anthropicKey) is honoured ONLY for
// Anthropic — an operator who named a key for one vendor must never have it put
// on the wire to a third-party OpenAI-compatible host by a provider switch.
func ResolveKey(provider, apiKey, anthropicKey string) string {
	if apiKey != "" {
		return apiKey
	}
	if NeedsKey(provider) {
		return anthropicKey
	}
	return ""
}

// ----- ollama native chat (contract §6.1) -----

type ollamaMessage struct {
	Role    string   `json:"role"`
	Content string   `json:"content"`
	Images  []string `json:"images,omitempty"` // bare base64, no data: prefix
}

type ollamaRequest struct {
	Model    string          `json:"model"`
	Stream   bool            `json:"stream"`
	Messages []ollamaMessage `json:"messages"`
}

type ollamaResponse struct {
	Model   string `json:"model"`
	Message struct {
		Content string `json:"content"`
	} `json:"message"`
	Error string `json:"error"`
}

func (c *Client) chatOllama(ctx context.Context, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
	body := ollamaRequest{Model: model, Stream: false,
		Messages: make([]ollamaMessage, 0, 1+len(req.Messages))}
	if req.System != "" {
		body.Messages = append(body.Messages, ollamaMessage{Role: "system", Content: req.System})
	}
	for _, m := range req.Messages {
		msg := ollamaMessage{Role: m.Role, Content: m.Text}
		for _, img := range m.Images {
			msg.Images = append(msg.Images, img.Data)
		}
		body.Messages = append(body.Messages, msg)
	}
	raw, status, err := c.postJSON(ctx, c.base+ollamaChatPath, body, nil)
	if err != nil {
		return protocol.LlmChatResponse{}, err
	}
	var parsed ollamaResponse
	if status < 200 || status >= 300 {
		// Ollama reports failures as {"error":"…"} with no type field — surface it
		// so a missing model reads as itself, not as a bare status code.
		if json.Unmarshal(raw, &parsed) == nil && parsed.Error != "" {
			return protocol.LlmChatResponse{}, c.statusErr(status, "", parsed.Error)
		}
		return protocol.LlmChatResponse{}, c.statusErr(status, "", "")
	}
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return protocol.LlmChatResponse{}, fmt.Errorf("llm: invalid response JSON: %w", err)
	}
	outModel := parsed.Model
	if outModel == "" {
		outModel = model
	}
	// Ollama has no stopReason field; a completed turn is an ordinary end_turn.
	return protocol.LlmChatResponse{Model: outModel, Text: parsed.Message.Content, StopReason: "end_turn"}, nil
}

// ----- OpenAI-compatible chat completions (contract §6.2) -----

type openAIContent struct {
	Type     string `json:"type"`
	Text     string `json:"text,omitempty"`
	ImageURL *struct {
		URL string `json:"url"`
	} `json:"image_url,omitempty"`
}

type openAIMessage struct {
	Role string `json:"role"`
	// string for text-only turns, []openAIContent when images ride along —
	// the shape the API itself documents.
	Content any `json:"content"`
}

type openAIRequest struct {
	Model    string          `json:"model"`
	Stream   bool            `json:"stream"`
	Messages []openAIMessage `json:"messages"`
}

type openAIResponse struct {
	Model   string `json:"model"`
	Choices []struct {
		Message struct {
			Content string `json:"content"`
		} `json:"message"`
		FinishReason string `json:"finish_reason"`
	} `json:"choices"`
	Error struct {
		Message string `json:"message"`
		// type/code carry the machine-readable reason (invalid_api_key,
		// insufficient_quota, model_not_found) — more reliable than the prose.
		Type string `json:"type"`
		Code any    `json:"code"` // string on OpenAI, a number on some clones
	} `json:"error"`
}

func (c *Client) chatOpenAI(ctx context.Context, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
	body := openAIRequest{Model: model, Stream: false,
		Messages: make([]openAIMessage, 0, 1+len(req.Messages))}
	if req.System != "" {
		body.Messages = append(body.Messages, openAIMessage{Role: "system", Content: req.System})
	}
	for _, m := range req.Messages {
		if len(m.Images) == 0 {
			body.Messages = append(body.Messages, openAIMessage{Role: m.Role, Content: m.Text})
			continue
		}
		parts := make([]openAIContent, 0, 1+len(m.Images))
		parts = append(parts, openAIContent{Type: "text", Text: m.Text})
		for _, img := range m.Images {
			part := openAIContent{Type: "image_url"}
			part.ImageURL = &struct {
				URL string `json:"url"`
			}{URL: "data:" + img.MediaType + ";base64," + img.Data}
			parts = append(parts, part)
		}
		body.Messages = append(body.Messages, openAIMessage{Role: m.Role, Content: parts})
	}
	headers := map[string]string{}
	if c.key != "" { // optional: LM Studio / llama.cpp need none
		headers["Authorization"] = "Bearer " + c.key
	}
	raw, status, err := c.postJSON(ctx, c.base+openAIChatPath, body, headers)
	if err != nil {
		return protocol.LlmChatResponse{}, err
	}
	var parsed openAIResponse
	if status < 200 || status >= 300 {
		if json.Unmarshal(raw, &parsed) == nil && parsed.Error.Message != "" {
			errType := parsed.Error.Type
			if code, ok := parsed.Error.Code.(string); ok && code != "" {
				errType = strings.TrimSpace(errType + " " + code)
			}
			return protocol.LlmChatResponse{}, c.statusErr(status, errType, parsed.Error.Message)
		}
		return protocol.LlmChatResponse{}, c.statusErr(status, "", "")
	}
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return protocol.LlmChatResponse{}, fmt.Errorf("llm: invalid response JSON: %w", err)
	}
	out := protocol.LlmChatResponse{Model: parsed.Model}
	if out.Model == "" {
		out.Model = model
	}
	if len(parsed.Choices) > 0 {
		out.Text = parsed.Choices[0].Message.Content
		// Map the OpenAI finish reason onto the contract's stopReason vocabulary
		// so clients apply the SAME truncation rule for every provider (§6.3).
		switch strings.ToLower(parsed.Choices[0].FinishReason) {
		case "length":
			out.StopReason = "max_tokens"
		case "content_filter":
			out.StopReason = "refusal"
		default:
			out.StopReason = "end_turn"
		}
	}
	return out, nil
}

// postJSON encodes body, POSTs it with the given extra headers, and returns the
// (bounded) response bytes and status. Shared by every provider mapping so the
// response cap and redirect policy can't drift between them.
func (c *Client) postJSON(ctx context.Context, url string, body any, headers map[string]string) ([]byte, int, error) {
	payload, err := json.Marshal(body)
	if err != nil {
		return nil, 0, fmt.Errorf("llm: encode request: %w", err)
	}
	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, url, strings.NewReader(string(payload)))
	if err != nil {
		return nil, 0, fmt.Errorf("llm: build request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")
	for k, v := range headers {
		httpReq.Header.Set(k, v)
	}
	resp, err := c.http.Do(httpReq)
	if err != nil {
		return nil, 0, c.transportErr(err)
	}
	defer resp.Body.Close()
	raw, err := readBounded(resp)
	if err != nil {
		return nil, 0, err
	}
	return raw, resp.StatusCode, nil
}
