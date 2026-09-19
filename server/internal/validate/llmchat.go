package validate

// Per-request bounds on one /llm/chat turn. The clients already keep payloads
// small (llm-contract.md §7), but this forwards on the operator's key, so the
// limits are re-imposed rather than trusted — the body cap alone would allow a
// 32 MiB single-turn prompt.

import (
	"encoding/base64"
	"fmt"
	"io"
	"regexp"
	"strings"

	"stencil/server/internal/protocol"
)

const (
	// MaxLLMImages caps the total image blocks in one chat request.
	MaxLLMImages = 10
	// MaxLLMMessages mirrors §7's replay window.
	MaxLLMMessages = 32
	// MaxLLMTextBytes bounds system + every message's text together.
	MaxLLMTextBytes = 256 * 1024
	// MaxLLMModelLen bounds the client-chosen model name forwarded upstream.
	MaxLLMModelLen = 64
	// MaxLLMSystemBytes bounds the system prompt alone; a real prompt (head + op
	// bullets + tail + short dynamic suffix) sits far below it.
	MaxLLMSystemBytes = 32 * 1024
)

// llmModelPattern bounds the model name: a client-supplied string that goes straight into
// the upstream request, so keep it to the character set real model ids use.
var llmModelPattern = regexp.MustCompile(`^[A-Za-z0-9._:-]+$`)

// llmImageMediaTypes is the attachment media-type allowlist.
var llmImageMediaTypes = map[string]bool{
	"image/png":  true,
	"image/jpeg": true,
	"image/webp": true,
	"image/gif":  true,
}

// LLMChat checks a chat request before it reaches the provider: "" when valid, else a client-facing
// message. systemOK recognizes Stencil's own system prompts, whose pinned heads live with the asset.
func LLMChat(req protocol.LlmChatRequest, systemOK func(string) bool) string {
	if len(req.Messages) == 0 {
		return "at least one message is required"
	}
	if len(req.Messages) > MaxLLMMessages {
		return fmt.Sprintf("at most %d messages per request", MaxLLMMessages)
	}
	if req.Model != "" && (len(req.Model) > MaxLLMModelLen || !llmModelPattern.MatchString(req.Model)) {
		return "model is not a valid model name"
	}
	// Interim pin: only Stencil's own prompt heads — or an empty system, harmless
	// without a custom prompt — ride out on the operator's key.
	if len(req.System) > MaxLLMSystemBytes {
		return fmt.Sprintf("the system prompt exceeds %d bytes", MaxLLMSystemBytes)
	}
	if req.System != "" && !systemOK(req.System) {
		return "unrecognized system prompt"
	}
	textBytes := len(req.System)
	images := 0
	for _, m := range req.Messages {
		textBytes += len(m.Text)
		if textBytes > MaxLLMTextBytes {
			return fmt.Sprintf("the request text exceeds %d bytes", MaxLLMTextBytes)
		}
		if m.Role != "user" && m.Role != "assistant" {
			return "message role must be user or assistant"
		}
		if m.Text == "" && len(m.Images) == 0 {
			return "message text or images required"
		}
		for _, img := range m.Images {
			images++
			if images > MaxLLMImages {
				return fmt.Sprintf("at most %d images per request", MaxLLMImages)
			}
			if !llmImageMediaTypes[img.MediaType] {
				return "unsupported image media type " + img.MediaType
			}
			// Stream through the decoder (no decoded copy kept) to verify the
			// payload is non-empty valid base64.
			n, err := io.Copy(io.Discard, base64.NewDecoder(base64.StdEncoding, strings.NewReader(img.Data)))
			if err != nil || n == 0 {
				return "image data is not valid base64"
			}
		}
	}
	return ""
}
