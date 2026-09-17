// Package httpapi is the REST surface of the server: token issuance, project
// CRUD, and file upload/download. It is built on net/http's method+pattern
// ServeMux (no router dependency). Handlers depend on narrow interfaces
// (ProjectStore, SessionStore, FileStore, eventbus.Bus) so they can be unit-tested
// without a live database.
package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"net/netip"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/ratelimit"
	"stencil/server/internal/service"
)

// Deps bundles everything the API handlers require.
type Deps struct {
	Projects     ProjectStore
	Sessions     SessionStore
	Files        FileStore
	LiveSessions SessionCounter // optional: live edit-session connection counts (the hub)
	LLM          LLM            // optional: Anthropic proxy; nil = LLM routes disabled
	Bus          eventbus.Bus
	TokenTTL     time.Duration
	ProjectTTL   time.Duration // default project lifetime; 0 = no expiry (off)
	MaxBodyBytes int64
	AdminToken   string // when set, gates POST /auth/token
	AuthOpen     bool   // opt-in open issuance: POST /auth/token needs no admin bearer
	// Spend controls for /llm/chat (llmlimit.go); 0 = unlimited.
	LLMRatePerMin  int // per-session turns per minute
	LLMMaxInFlight int // concurrent upstream calls, server-wide
	// Abuse guards for the non-LLM writes (ratelimit.go); 0 = off.
	AuthRatePerMin  int // POST /auth/token attempts per minute, per client IP
	WriteRatePerMin int // project creations + file uploads per minute, per session
	// Peers whose X-Forwarded-For is believed when keying a per-IP limiter;
	// empty (the default) ignores the header entirely.
	TrustedProxies []netip.Prefix
	OpTimeout      time.Duration // deadline around ONE store operation
}

// API holds the resolved dependencies and serves HTTP. The project and file
// policy it enforces lives in internal/service, composed here from Deps so the
// handlers stay transport-only.
type API struct {
	deps      Deps
	projects  *service.ProjectService
	files     *service.FileService
	llmRate   *ratelimit.Limiter // per-session, POST /llm/chat
	llmGate   *llmGate
	authRate  *ratelimit.Limiter // per-IP, POST /auth/token
	writeRate *ratelimit.Limiter // per-session, project creation + file uploads
}

// New constructs the API handler set.
func New(deps Deps) *API {
	if deps.MaxBodyBytes == 0 {
		deps.MaxBodyBytes = 32 << 20
	}
	if deps.TokenTTL == 0 {
		deps.TokenTTL = 7 * 24 * time.Hour
	}
	if deps.OpTimeout <= 0 {
		deps.OpTimeout = defaultOpTimeout
	}
	return &API{
		deps:      deps,
		projects:  service.NewProjects(deps.Projects, deps.Files, liveSessions(deps), deps.Bus, deps.ProjectTTL),
		files:     service.NewFiles(deps.Projects, deps.Files, deps.Bus),
		llmRate:   ratelimit.New(deps.LLMRatePerMin),
		llmGate:   newLLMGate(deps.LLMMaxInFlight),
		authRate:  ratelimit.New(deps.AuthRatePerMin),
		writeRate: ratelimit.New(deps.WriteRatePerMin),
	}
}

// liveSessions hands the hub to the service as a plain interface, keeping a
// typed nil (an unset Deps.LiveSessions) out of it.
func liveSessions(deps Deps) service.SessionCounter {
	if deps.LiveSessions == nil {
		return nil
	}
	return deps.LiveSessions
}

// defaultOpTimeout is the fallback for Deps.OpTimeout (OP_TIMEOUT_SECONDS).
const defaultOpTimeout = 10 * time.Second

// opCtx bounds one store operation: the request context alone runs to the
// server's 5-minute write timeout, which is a long time to pin a pool
// connection. Streaming a download still uses the request itself.
func (a *API) opCtx(req *http.Request) (context.Context, context.CancelFunc) {
	return context.WithTimeout(req.Context(), a.deps.OpTimeout)
}

// Register mounts the REST routes onto mux. Project/file routes are gated by the
// auth middleware; POST /auth/token has its own admin gate.
func (a *API) Register(mux *http.ServeMux) {
	mux.HandleFunc("POST /auth/token", a.limitByIP(a.authRate, a.handleIssueToken))

	guard := auth.Middleware(a.deps.Sessions)
	protected := map[string]http.HandlerFunc{
		"GET /projects":                      a.handleListProjects,
		"POST /projects":                     limitBySession(a.writeRate, a.handleCreateProject),
		"GET /projects/{id}":                 a.handleGetProject,
		"PUT /projects/{id}":                 a.handleUpdateProject,
		"DELETE /projects/{id}":              a.handleDeleteProject,
		"GET /projects/{id}/files/{kind}":    a.handleGetFile,
		"POST /projects/{id}/files/{kind}":   limitBySession(a.writeRate, a.handlePutFile),
		"DELETE /projects/{id}/files/{kind}": a.handleDeleteFile,
		"GET /llm/info":                      a.handleLLMInfo,
		"POST /llm/chat":                     a.handleLLMChat,
	}
	for pattern, h := range protected {
		mux.Handle(pattern, guard(h))
	}
}

func (a *API) Handler() *http.ServeMux {
	mux := http.NewServeMux()
	a.Register(mux)
	return mux
}

// ----- shared response helpers -----

func writeJSON(rw http.ResponseWriter, status int, v any) {
	rw.Header().Set("Content-Type", "application/json")
	rw.WriteHeader(status)
	_ = json.NewEncoder(rw).Encode(v)
}

func writeErr(rw http.ResponseWriter, status int, code, msg string) {
	writeJSON(rw, status, protocol.ErrorResponse{Code: code, Message: msg})
}

func writeBadRequest(rw http.ResponseWriter, msg string) {
	writeErr(rw, http.StatusBadRequest, protocol.CodeBadRequest, msg)
}

func writeNotFound(rw http.ResponseWriter, msg string) {
	writeErr(rw, http.StatusNotFound, protocol.CodeNotFound, msg)
}

func writeInternalError(rw http.ResponseWriter, msg string) {
	writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msg)
}

// decodeJSON reads a JSON body with a size cap and strict unknown-field
// rejection.
func (a *API) decodeJSON(rw http.ResponseWriter, req *http.Request, dst any) bool {
	req.Body = http.MaxBytesReader(rw, req.Body, a.deps.MaxBodyBytes)
	dec := json.NewDecoder(req.Body)
	dec.DisallowUnknownFields()
	if err := dec.Decode(dst); err != nil {
		writeBadRequest(rw, msgInvalidJSONPre+err.Error())
		return false
	}
	return true
}
