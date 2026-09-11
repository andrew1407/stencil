package llm

// The OpenAI-compatible chat-completions mapping (llm-contract.md §6.2) — LM
// Studio, llama.cpp and anything else speaking the same route.

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"

	"stencil/server/internal/protocol"
)

const openAIChatPath = "/chat/completions"

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

// openAIMapping is the mapping for any OpenAI-compatible server.
type openAIMapping struct{}

func (openAIMapping) chat(ctx context.Context, c *Client, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
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
