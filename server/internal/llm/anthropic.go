// Package llm is the server-side Anthropic Messages API client behind the
// /llm/* proxy routes. Per llm-contract.md, the API key lives only in this
// server's environment: clients speak protocol.LlmChatRequest/Response and
// never see Anthropic's wire format or the key. Stdlib net/http only,
// non-streaming (v1). Error strings and logs never carry the key or image
// payloads.
package llm

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"stencil/server/internal/protocol"
)

// Anthropic Messages API constants (llm-contract.md §6.3).
const (
	messagesPath     = "/v1/messages"
	anthropicVersion = "2023-06-01"

	// maxResponseBytes caps how much of an upstream response body is read.
	// LLM_BASE_URL is configurable, so a broken/hostile upstream must not be
	// able to grow the heap per in-flight request for the full timeout window;
	// legitimate Messages API responses are well under 1 MiB.
	maxResponseBytes = 8 << 20 // 8 MiB
)

// Doer is the outbound HTTP seam: *http.Client satisfies it, tests inject a
// mock to assert the exact request without a network.
type Doer interface {
	Do(*http.Request) (*http.Response, error)
}

// Client proxies chat turns to the configured upstream (Anthropic, Ollama or
// any OpenAI-compatible server — llm-contract.md §6).
type Client struct {
	provider  string
	base      string
	key       string
	model     string
	maxTokens int
	http      Doer
}

// New builds a Client whose outbound calls are bounded by timeout. `provider`
// is one of the §6 mappings (empty = anthropic, the historical default).
func New(provider, base, key, model string, maxTokens int, timeout time.Duration) *Client {
	if provider == "" {
		provider = ProviderAnthropic
	}
	return &Client{
		provider:  provider,
		base:      strings.TrimRight(base, "/"),
		key:       key,
		model:     model,
		maxTokens: maxTokens,
		http: &http.Client{
			Timeout: timeout,
			// net/http strips Authorization on a cross-host redirect but not a custom
			// x-api-key, so a redirecting LLM_BASE_URL would leak the key. Don't follow:
			// the 30x surfaces as a non-2xx like any other upstream failure.
			CheckRedirect: func(*http.Request, []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
	}
}

// Model returns the server-default model (reported by GET /llm/info).
func (c *Client) Model() string { return c.model }

// Provider returns the configured upstream mapping.
func (c *Client) Provider() string { return c.provider }

// readBounded reads at most maxResponseBytes of an upstream body: LLM_BASE_URL
// is configurable, so a broken/hostile upstream must not grow the heap per
// in-flight request. Shared by every provider mapping.
func readBounded(resp *http.Response) ([]byte, error) {
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxResponseBytes+1))
	if err != nil {
		return nil, fmt.Errorf("llm: read response: %w", err)
	}
	if len(raw) > maxResponseBytes {
		return nil, fmt.Errorf("llm: response body exceeds %d bytes", maxResponseBytes)
	}
	return raw, nil
}

// ----- Anthropic wire shapes (POST /v1/messages) -----

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

// Chat translates one protocol chat turn to the Anthropic Messages API and
// back. The request's MaxTokens is clamped to the server cap and an empty
// request model falls back to the server default.
// Chat runs one turn against the configured provider. The canonical
// request/response is the same for all three — only the wire shape differs.
func (c *Client) Chat(ctx context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error) {
	model := req.Model
	if model == "" {
		model = c.model
	}
	switch c.provider {
	case ProviderOllama:
		return c.chatOllama(ctx, req, model)
	case ProviderOpenAI:
		return c.chatOpenAI(ctx, req, model)
	}
	return c.chatAnthropic(ctx, req, model)
}

func (c *Client) chatAnthropic(ctx context.Context, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error) {
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
