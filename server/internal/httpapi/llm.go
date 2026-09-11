package httpapi

import (
	"errors"
	"log"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/validate"
)

// upstreamError is the llm package's classified-failure seam (*llm.UpstreamError):
// an error carrying a sanitized, client-safe reason for an UPSTREAM condition the
// user can act on — no credits, bad key, unknown model, timeout, unreachable host.
type upstreamError interface {
	error
	ClientMessage() string
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
		writeErr(w, http.StatusServiceUnavailable, protocol.CodeLlmDisabled, msgLlmDisabled)
		return
	}
	// Rate FIRST, before the body is read or decoded: a client that is over its
	// budget shouldn't get to spend the server's memory either.
	sess, _ := auth.SessionFromContext(r.Context())
	if !a.llmRate.Allow(sess.ID) {
		w.Header().Set("Retry-After", "60")
		writeErr(w, http.StatusTooManyRequests, protocol.CodeRateLimited, msgLlmRateLimited)
		return
	}
	var req protocol.LlmChatRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	if msg := validate.LLMChat(req, hasStencilPromptHead); msg != "" {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msg)
		return
	}
	release, ok := a.llmGate.enter()
	if !ok {
		w.Header().Set("Retry-After", "5")
		writeErr(w, http.StatusTooManyRequests, protocol.CodeRateLimited, msgLlmBusy)
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
		writeErr(w, http.StatusBadGateway, protocol.CodeInternal, msgLlmFailed)
		return
	}
	writeJSON(w, http.StatusOK, resp)
}
