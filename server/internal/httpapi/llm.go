package httpapi

import (
	"errors"
	"log"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/validate"
)

// handleLLMInfo reports whether the Anthropic proxy is configured and with
// which default model, so settings UIs can render "via server X (model)".
func (a *API) handleLLMInfo(rw http.ResponseWriter, _ *http.Request) {
	if a.deps.LLM == nil {
		writeJSON(rw, http.StatusOK, protocol.LlmInfoResponse{Enabled: false})
		return
	}
	writeJSON(rw, http.StatusOK, protocol.LlmInfoResponse{Enabled: true, Model: a.deps.LLM.Model()})
}

// handleLLMChat validates one chat turn and forwards it to the configured LLM.
func (a *API) handleLLMChat(rw http.ResponseWriter, req *http.Request) {
	if a.deps.LLM == nil {
		writeErr(rw, http.StatusServiceUnavailable, protocol.CodeLlmDisabled, msgLlmDisabled)
		return
	}
	// Rate FIRST, before the body is read or decoded: a client that is over its
	// budget shouldn't get to spend the server's memory either.
	sess, _ := auth.SessionFromContext(req.Context())
	if !a.llmRate.Allow(sess.ID) {
		rw.Header().Set("Retry-After", "60")
		writeErr(rw, http.StatusTooManyRequests, protocol.CodeRateLimited, msgLlmRateLimited)
		return
	}
	var body protocol.LlmChatRequest
	if !a.decodeJSON(rw, req, &body) {
		return
	}
	if msg := validate.LLMChat(body, hasStencilPromptHead); msg != "" {
		writeBadRequest(rw, msg)
		return
	}
	release, ok := a.llmGate.enter()
	if !ok {
		rw.Header().Set("Retry-After", "5")
		writeErr(rw, http.StatusTooManyRequests, protocol.CodeRateLimited, msgLlmBusy)
		return
	}
	defer release()
	resp, err := a.deps.LLM.Chat(req.Context(), body)
	if err != nil {
		// The client going away mid-call is not an upstream failure; there is
		// nobody left to answer, so don't report it as a 502.
		if req.Context().Err() != nil {
			return
		}
		// Full detail (upstream envelope, dial errors) still goes to the server
		// log; the client gets the sanitized reason so the user can act on it
		// instead of guessing (llm-contract.md §6.3).
		log.Printf("llm: chat proxy failed: %v", err)
		var up upstreamError
		if errors.As(err, &up) {
			writeErr(rw, http.StatusBadGateway, protocol.CodeLlmUpstream, up.ClientMessage())
			return
		}
		// Genuinely internal faults keep the package's fixed-message style.
		writeErr(rw, http.StatusBadGateway, protocol.CodeInternal, msgLlmFailed)
		return
	}
	writeJSON(rw, http.StatusOK, resp)
}
