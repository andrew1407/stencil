package httpapi

import (
	"encoding/base64"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"regexp"
	"strings"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

// upstreamError is the llm package's classified-failure seam (*llm.UpstreamError):
// an error carrying a sanitized, client-safe reason for an UPSTREAM condition the
// user can act on — no credits, bad key, unknown model, timeout, unreachable host.
type upstreamError interface {
	error
	ClientMessage() string
}

// Per-request bounds. The clients already keep payloads small (llm-contract.md §7),
// but this forwards to Anthropic on the operator's key, so the limits are re-imposed here
// rather than trusted — the body cap alone would allow a 32 MiB single-turn prompt.
const (
	// maxLLMImages caps the total image blocks in one chat request.
	maxLLMImages = 10
	// maxLLMMessages mirrors §7's replay window.
	maxLLMMessages = 32
	// maxLLMTextBytes bounds system + every message's text together.
	maxLLMTextBytes = 256 * 1024
	// maxLLMModelLen bounds the client-chosen model name forwarded upstream.
	maxLLMModelLen = 64
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

// handleLLMInfo reports whether the Anthropic proxy is configured and with
// which default model, so settings UIs can render "via server X (model)".
func (a *API) handleLLMInfo(w http.ResponseWriter, _ *http.Request) {
	if a.deps.LLM == nil {
		writeJSON(w, http.StatusOK, protocol.LlmInfoResponse{Enabled: false})
		return
	}
	writeJSON(w, http.StatusOK, protocol.LlmInfoResponse{Enabled: true, Model: a.deps.LLM.Model()})
}

// handleLLMChat validates one chat turn and forwards it to the configured LLM.
func (a *API) handleLLMChat(w http.ResponseWriter, r *http.Request) {
	if a.deps.LLM == nil {
		writeErr(w, http.StatusServiceUnavailable, protocol.CodeLlmDisabled, "LLM proxy is not configured on this server")
		return
	}
	// Rate FIRST, before the body is read or decoded: a client that is over its
	// budget shouldn't get to spend the server's memory either.
	sess, _ := auth.SessionFromContext(r.Context())
	if !a.llmRate.allow(sess.ID) {
		w.Header().Set("Retry-After", "60")
		writeErr(w, http.StatusTooManyRequests, protocol.CodeRateLimited,
			"too many assistant requests — wait a moment and try again")
		return
	}
	var req protocol.LlmChatRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	if msg := validateLLMChat(req); msg != "" {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msg)
		return
	}
	release, ok := a.llmGate.enter()
	if !ok {
		w.Header().Set("Retry-After", "5")
		writeErr(w, http.StatusTooManyRequests, protocol.CodeRateLimited,
			"the assistant is busy — too many requests in flight")
		return
	}
	defer release()
	resp, err := a.deps.LLM.Chat(r.Context(), req)
	if err != nil {
		// The client going away mid-call is not an upstream failure; there is
		// nobody left to answer, so don't report it as a 502.
		if r.Context().Err() != nil {
			return
		}
		// Full detail (upstream envelope, dial errors) still goes to the server
		// log; the client gets the sanitized reason so the user can act on it
		// instead of guessing (llm-contract.md §6.3).
		log.Printf("llm: chat proxy failed: %v", err)
		var up upstreamError
		if errors.As(err, &up) {
			writeErr(w, http.StatusBadGateway, protocol.CodeLlmUpstream, up.ClientMessage())
			return
		}
		// Genuinely internal faults keep the package's fixed-message style.
		writeErr(w, http.StatusBadGateway, protocol.CodeInternal, "LLM request failed")
		return
	}
	writeJSON(w, http.StatusOK, resp)
}

// validateLLMChat checks a chat request before it reaches the provider.
// Returns "" when valid, else a client-facing message.
func validateLLMChat(req protocol.LlmChatRequest) string {
	if len(req.Messages) == 0 {
		return "at least one message is required"
	}
	if len(req.Messages) > maxLLMMessages {
		return fmt.Sprintf("at most %d messages per request", maxLLMMessages)
	}
	if req.Model != "" && (len(req.Model) > maxLLMModelLen || !llmModelPattern.MatchString(req.Model)) {
		return "model is not a valid model name"
	}
	// Interim pin (llmprompt.go): only Stencil's own prompt heads — or an empty
	// system, harmless without a custom prompt — ride out on the operator's key.
	if len(req.System) > maxLLMSystemBytes {
		return fmt.Sprintf("the system prompt exceeds %d bytes", maxLLMSystemBytes)
	}
	if req.System != "" && !hasStencilPromptHead(req.System) {
		return "unrecognized system prompt"
	}
	textBytes := len(req.System)
	images := 0
	for _, m := range req.Messages {
		textBytes += len(m.Text)
		if textBytes > maxLLMTextBytes {
			return fmt.Sprintf("the request text exceeds %d bytes", maxLLMTextBytes)
		}
		if m.Role != "user" && m.Role != "assistant" {
			return "message role must be user or assistant"
		}
		if m.Text == "" && len(m.Images) == 0 {
			return "message text or images required"
		}
		for _, img := range m.Images {
			images++
			if images > maxLLMImages {
				return fmt.Sprintf("at most %d images per request", maxLLMImages)
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
