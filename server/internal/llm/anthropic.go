package llm

// The Anthropic Messages API mapping (llm-contract.md §6.3).

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"

	"stencil/server/internal/protocol"
)

const (
	messagesPath     = "/v1/messages"
	anthropicVersion = "2023-06-01"
)

type anthropicRequest struct {
	Model     string             `json:"model"`
	MaxTokens int                `json:"max_tokens"`
	System    string             `json:"system,omitempty"`
	Messages  []anthropicMessage `json:"messages"`
}

type anthropicMessage struct {
	Role    string             `json:"role"`
	Content []anthropicContent `json:"content"`
}

type anthropicContent struct {
	Type   string           `json:"type"` // "text" | "image"
	Text   string           `json:"text,omitempty"`
	Source *anthropicSource `json:"source,omitempty"`
}

type anthropicSource struct {
	Type      string `json:"type"` // always "base64"
	MediaType string `json:"media_type"`
	Data      string `json:"data"`
}

type anthropicResponse struct {
	Model      string `json:"model"`
	StopReason string `json:"stop_reason"`
	Content    []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	} `json:"content"`
}

// anthropicError is the non-2xx envelope {type:"error",error:{type,message}}.
type anthropicError struct {
	Type  string `json:"type"`
	Error struct {
		Type    string `json:"type"`
		Message string `json:"message"`
	} `json:"error"`
}

// anthropicMapping is the default mapping (provider "" or "anthropic").
type anthropicMapping struct{}

// chat translates one protocol chat turn to the Anthropic Messages API and
// back. The request's MaxTokens is clamped to the server cap.
func (anthropicMapping) chat(ctx context.Context, c *Client, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
	maxTokens := req.MaxTokens
	if maxTokens <= 0 || maxTokens > c.maxTokens {
		maxTokens = c.maxTokens
	}

	body := anthropicRequest{
		Model: model, MaxTokens: maxTokens, System: req.System,
		Messages: make([]anthropicMessage, 0, len(req.Messages)),
	}
	for _, m := range req.Messages {
		msg := anthropicMessage{Role: m.Role, Content: make([]anthropicContent, 0, 1+len(m.Images))}
		if m.Text != "" {
			msg.Content = append(msg.Content, anthropicContent{Type: "text", Text: m.Text})
		}
		for _, img := range m.Images {
			msg.Content = append(msg.Content, anthropicContent{
				Type:   "image",
				Source: &anthropicSource{Type: "base64", MediaType: img.MediaType, Data: img.Data},
			})
		}
		body.Messages = append(body.Messages, msg)
	}
	payload, err := json.Marshal(body)
	if err != nil {
		return protocol.LlmChatResponse{}, fmt.Errorf("llm: encode request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.base+messagesPath, bytes.NewReader(payload))
	if err != nil {
		return protocol.LlmChatResponse{}, fmt.Errorf("llm: build request: %w", err)
	}
	httpReq.Header.Set("x-api-key", c.key)
	httpReq.Header.Set("anthropic-version", anthropicVersion)
	httpReq.Header.Set("Content-Type", "application/json")

	resp, err := c.http.Do(httpReq)
	if err != nil {
		return protocol.LlmChatResponse{}, c.transportErr(err)
	}
	defer resp.Body.Close()
	raw, err := readBounded(resp)
	if err != nil {
		return protocol.LlmChatResponse{}, err
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		// The {type,message} envelope is what says WHY (out of credits, bad key,
		// unknown model); classify it rather than collapsing to a status.
		var envelope anthropicError
		if json.Unmarshal(raw, &envelope) == nil && envelope.Error.Message != "" {
			return protocol.LlmChatResponse{}, c.statusErr(resp.StatusCode, envelope.Error.Type, envelope.Error.Message)
		}
		return protocol.LlmChatResponse{}, c.statusErr(resp.StatusCode, "", "")
	}

	var parsed anthropicResponse
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return protocol.LlmChatResponse{}, fmt.Errorf("llm: invalid response JSON: %w", err)
	}
	var text strings.Builder
	for _, block := range parsed.Content {
		if block.Type == "text" {
			text.WriteString(block.Text)
		}
	}
	outModel := parsed.Model
	if outModel == "" {
		outModel = model
	}
	return protocol.LlmChatResponse{Model: outModel, Text: text.String(), StopReason: parsed.StopReason}, nil
}
