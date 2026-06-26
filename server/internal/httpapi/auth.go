package httpapi

import (
	"crypto/subtle"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

// issueTokenRequest is the optional body of POST /auth/token.
type issueTokenRequest struct {
	Label string `json:"label,omitempty"`
}

// handleIssueToken mints a new bearer token + session. When an admin token is
// configured, the caller must present it (Authorization: Bearer <admin> or
// X-Admin-Token); otherwise issuance is open (development mode).
func (a *API) handleIssueToken(w http.ResponseWriter, r *http.Request) {
	if !a.adminAuthorized(r) {
		writeErr(w, http.StatusUnauthorized, protocol.CodeUnauthorized, "admin token required to issue tokens")
		return
	}
	var req issueTokenRequest
	if r.ContentLength != 0 {
		if !a.decodeJSON(w, r, &req) {
			return
		}
	}

	token, hash, err := auth.GenerateToken()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "token generation failed")
		return
	}
	now := nowMs()
	expires := now + a.deps.TokenTTL.Milliseconds()
	if _, err := a.deps.Sessions.CreateSession(r.Context(), hash, req.Label, now, expires); err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not persist session")
		return
	}
	writeJSON(w, http.StatusOK, protocol.TokenResponse{Token: token, ExpiresAt: expires})
}

// adminAuthorized reports whether the request may issue tokens.
func (a *API) adminAuthorized(r *http.Request) bool {
	if a.deps.AdminToken == "" {
		return true // open issuance when no admin token is configured
	}
	presented := r.Header.Get("X-Admin-Token")
	if presented == "" {
		presented = auth.BearerToken(r)
	}
	return subtle.ConstantTimeCompare([]byte(presented), []byte(a.deps.AdminToken)) == 1
}
