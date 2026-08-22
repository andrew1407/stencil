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

// handleIssueToken mints a new bearer token + session. The caller must present
// the admin token (Authorization: Bearer <admin> or X-Admin-Token); with no
// admin token configured, issuance is closed outright — config.Load generates a
// per-boot token precisely so this can never be open by accident. AUTH_OPEN
// (Deps.AuthOpen) is the explicit opt-out: issuance succeeds with no bearer at
// all, though the per-IP rate limit and the admin token both keep working.
func (a *API) handleIssueToken(w http.ResponseWriter, r *http.Request) {
	if !a.deps.AuthOpen && !a.adminAuthorized(r) {
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
		return false // fail closed: no admin token means nobody can issue
	}
	presented := r.Header.Get("X-Admin-Token")
	if presented == "" {
		presented = auth.BearerToken(r)
	}
	return subtle.ConstantTimeCompare([]byte(presented), []byte(a.deps.AdminToken)) == 1
}
