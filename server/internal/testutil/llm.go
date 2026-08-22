package testutil

import (
	"context"

	"stencil/server/internal/protocol"
)

// EchoLLM is a fake httpapi.LLM: it records the last request it saw and echoes
// the final user message back (or fails with Err when set).
type EchoLLM struct {
	LastReq protocol.LlmChatRequest
	Err     error
}

func (f *EchoLLM) Chat(_ context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error) {
	f.LastReq = req
	if f.Err != nil {
		return protocol.LlmChatResponse{}, f.Err
	}
	text := ""
	if n := len(req.Messages); n > 0 {
		text = "echo: " + req.Messages[n-1].Text
	}
	return protocol.LlmChatResponse{Model: "claude-test", Text: text, StopReason: "end_turn"}, nil
}

func (f *EchoLLM) Model() string { return "claude-test" }
