package llm

// The Ollama native chat mapping (llm-contract.md §6.1).

import (
	"context"
	"encoding/json"
	"fmt"

	"stencil/server/internal/protocol"
)

const ollamaChatPath = "/api/chat"

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

// ollamaMapping is the mapping for a local Ollama server.
type ollamaMapping struct{}

func (ollamaMapping) chat(ctx context.Context, c *Client, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
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
