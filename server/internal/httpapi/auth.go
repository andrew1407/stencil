package httpapi

import (
	"crypto/subtle"
	"errors"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/validate"
)

// issueTokenRequest is the optional body of POST /auth/token.
type issueTokenRequest struct {
	Label string `json:"label,omitempty"`
}

// handleIssueToken mints a bearer token + session; the caller must present the admin token. With none
// configured issuance is closed outright, and AUTH_OPEN (Deps.AuthOpen) is the explicit opt-out.
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
	if err := validate.TokenLabel(body.Label); err != nil {
		writeBadRequest(rw, err.Error())
		return
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

// handleSession answers the bearer's own session: the cheap token probe (the guard's one indexed lookup)
// that replaces listing every project. The admin token is no session, so it still gets 401 here.
func (a *API) handleSession(rw http.ResponseWriter, req *http.Request) {
	sess, _ := auth.SessionFromContext(req.Context())
	writeJSON(rw, http.StatusOK, protocol.SessionResponse{SessionID: sess.ID, ExpiresAt: sess.ExpiresAt})
}

// handleRevokeOwnSession ends the bearer's own session: its token stops resolving at once and its live
// feeds are hung up, on every instance.
func (a *API) handleRevokeOwnSession(rw http.ResponseWriter, req *http.Request) {
	sess, _ := auth.SessionFromContext(req.Context())
	a.revokeSession(rw, req, sess.ID)
}

// handleRevokeSession is the admin's revocation of one session by id (a leaked token).
func (a *API) handleRevokeSession(rw http.ResponseWriter, req *http.Request) {
	if !a.adminAuthorized(req) {
		writeErr(rw, http.StatusUnauthorized, protocol.CodeUnauthorized, msgAdminRevoke)
		return
	}
	a.revokeSession(rw, req, req.PathValue("id"))
}

// handleRevokeAllSessions is the admin's revocation of every session, e.g. after rotating ADMIN_TOKEN.
func (a *API) handleRevokeAllSessions(rw http.ResponseWriter, req *http.Request) {
	if !a.adminAuthorized(req) {
		writeErr(rw, http.StatusUnauthorized, protocol.CodeUnauthorized, msgAdminRevoke)
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	if _, err := a.deps.Sessions.DeleteAllSessions(ctx); err != nil {
		writeInternalError(rw, msgRevokeSession)
		return
	}
	eventbus.PublishSessionRevoked(ctx, a.deps.Bus, "")
	rw.WriteHeader(http.StatusNoContent)
}

func (a *API) revokeSession(rw http.ResponseWriter, req *http.Request, id string) {
	ctx, cancel := a.opCtx(req)
	defer cancel()
	err := a.deps.Sessions.DeleteSession(ctx, id)
	if errors.Is(err, store.ErrNotFound) {
		writeNotFound(rw, msgNoSession)
		return
	}
	if err != nil {
		writeInternalError(rw, msgRevokeSession)
		return
	}
	eventbus.PublishSessionRevoked(ctx, a.deps.Bus, id)
	rw.WriteHeader(http.StatusNoContent)
}
