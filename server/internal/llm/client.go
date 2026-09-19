// Package llm is the server-side Anthropic Messages API client behind the /llm/* proxy routes. Per
// llm-contract.md the API key lives only in this server's environment: clients speak
// protocol.LlmChatRequest/Response and never see the provider's wire format or the key. Stdlib
// net/http only, non-streaming (v1); error strings and logs never carry the key or image payloads.
package llm

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"stencil/server/internal/protocol"
)

// maxResponseBytes caps how much of an upstream response body is read: LLM_BASE_URL is configurable, so a
// hostile upstream must not grow the heap per in-flight request. Legitimate replies are well under 1 MiB.
const maxResponseBytes = 8 << 20 // 8 MiB

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
			// net/http strips Authorization on a cross-host redirect but not a custom x-api-key, so a redirecting
			// LLM_BASE_URL would leak the key. Don't follow: the 30x surfaces as a non-2xx like any other failure.
			CheckRedirect: func(*http.Request, []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
	}
}

// Model returns the server-default model (reported by GET /llm/info).
func (c *Client) Model() string { return c.model }

func (c *Client) Provider() string { return c.provider }

// Chat runs one turn against the configured provider. The canonical request/response is the same for all
// three — only the wire shape differs — so this dispatches to the provider's mapping (providers.go).
func (c *Client) Chat(ctx context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error) {
	model := req.Model
	if model == "" {
		model = c.model
	}
	return mappingFor(c.provider).chat(ctx, c, req, model)
}

// readBounded reads at most maxResponseBytes of an upstream body, so a hostile LLM_BASE_URL cannot grow
// the heap per in-flight request. Shared by every provider mapping.
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

// postJSON encodes body, POSTs it with the given extra headers, and returns the bounded response bytes
// and status. Shared by every provider mapping so the cap and redirect policy cannot drift.
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
