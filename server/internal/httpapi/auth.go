package httpapi

import (
	"crypto/subtle"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
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
func (a *API) handleIssueToken(rw http.ResponseWriter, req *http.Request) {
	if !a.deps.AuthOpen && !a.adminAuthorized(req) {
		writeErr(rw, http.StatusUnauthorized, protocol.CodeUnauthorized, msgAdminRequired)
		return
	}
	var body issueTokenRequest
	if req.ContentLength != 0 {
		if !a.decodeJSON(rw, req, &body) {
			return
		}
	}

	token, hash, err := auth.GenerateToken()
	if err != nil {
		writeInternalError(rw, msgTokenGenFailed)
		return
	}
	now := clock.NowMs()
	expires := now + a.deps.TokenTTL.Milliseconds()
	ctx, cancel := a.opCtx(req)
	defer cancel()
	if _, err := a.deps.Sessions.CreateSession(ctx, hash, body.Label, now, expires); err != nil {
		writeInternalError(rw, msgPersistSession)
		return
	}
	writeJSON(rw, http.StatusOK, protocol.TokenResponse{Token: token, ExpiresAt: expires})
}

// adminAuthorized reports whether the request may issue tokens.
func (a *API) adminAuthorized(req *http.Request) bool {
	if a.deps.AdminToken == "" {
		return false // fail closed: no admin token means nobody can issue
	}
	presented := req.Header.Get("X-Admin-Token")
	if presented == "" {
		presented = auth.BearerToken(req)
	}
	return subtle.ConstantTimeCompare([]byte(presented), []byte(a.deps.AdminToken)) == 1
}
