package validate

// Opt-in: `go test -bench . ./internal/validate/` (CI never passes -bench).
// Baselines are in server/README.md#benchmarks.

import (
	"encoding/base64"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

// benchChatRequest builds a worst-case-shaped turn: the full replay window, a
// near-cap system prompt, and images whose base64 must be decoded to be checked.
func benchChatRequest(images int) protocol.LlmChatRequest {
	png := base64.StdEncoding.EncodeToString([]byte(strings.Repeat("\x89PNG\r\n\x1a\n", 4096)))
	req := protocol.LlmChatRequest{System: strings.Repeat("op bullet line\n", 1200)}
	for i := 0; i < MaxLLMMessages; i++ {
		m := protocol.LlmMessage{Role: "user", Text: strings.Repeat("draw a box around the logo ", 40)}
		if i%2 == 1 {
			m.Role = "assistant"
		}
		if i < images {
			m.Images = []protocol.LlmImage{{MediaType: "image/png", Data: png}}
		}
		req.Messages = append(req.Messages, m)
	}
	return req
}

// BenchmarkLLMChat measures the per-request validation a proxied turn pays. The
// image scan is the cost: every attachment is base64-decoded through io.Discard.
func BenchmarkLLMChat(b *testing.B) {
	okSystem := func(string) bool { return true }
	for _, images := range []int{0, MaxLLMImages} {
		name := "images=none"
		if images > 0 {
			name = "images=max"
		}
		b.Run(name, func(b *testing.B) {
			req := benchChatRequest(images)
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				if msg := LLMChat(req, okSystem); msg != "" {
					b.Fatalf("request should be valid: %s", msg)
				}
			}
		})
	}
}
