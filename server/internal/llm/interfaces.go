package llm

import (
	"context"
	"net/http"

	"stencil/server/internal/protocol"
)

// Doer is the outbound HTTP seam: *http.Client satisfies it, tests inject a
// mock to assert the exact request without a network.
type Doer interface {
	Do(*http.Request) (*http.Response, error)
}

// providerMapping is one provider's wire mapping: the canonical request into that provider's shape and
// its reply back. Adding a provider is a table entry plus its own file — never an edit to Chat.
type providerMapping interface {
	chat(ctx context.Context, c *Client, req protocol.LlmChatRequest, model string) (protocol.LlmChatResponse, error)
}
