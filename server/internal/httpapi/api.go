// Package httpapi is the REST surface of the server: token issuance, project
// CRUD, and file upload/download. It is built on net/http's method+pattern
// ServeMux (no router dependency). Handlers depend on narrow interfaces
// (ProjectStore, SessionStore, FileStore, bus.Bus) so they can be unit-tested
// without a live database.
package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"os"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// ProjectStore is the project persistence the API needs.
type ProjectStore interface {
	ListProjects(ctx context.Context) ([]protocol.ProjectRecord, error)
	GetProject(ctx context.Context, id string) (protocol.ProjectRecord, error)
	CreateProject(ctx context.Context, ownerSession string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error)
	UpdateProject(ctx context.Context, id string, patch store.ProjectPatch, expectedVersion int64) (protocol.ProjectRecord, error)
	SetFile(ctx context.Context, id, kind, relPath string, w, h int) (protocol.ProjectRecord, error)
	DeleteProject(ctx context.Context, id string) error
}

// SessionStore issues and resolves sessions.
type SessionStore interface {
	auth.SessionResolver
	CreateSession(ctx context.Context, tokenHash []byte, label string, createdAt, expiresAt int64) (auth.Session, error)
}

// SessionCounter reports how many clients are currently connected to a project's
// live edit session. The hub satisfies it; it is optional (nil in unit tests), in
// which case the delete guard treats every project as having no live connections.
type SessionCounter interface {
	ConnectionCount(projectID string) int
}

// FileStore is the byte storage the API needs. FindByKind resolves the stored
// path for filestore-only kinds (video/variantN/chat); OpenByRelPath hands back
// an *os.File so downloads can stream via http.ServeContent instead of
// buffering whole files in memory; RemoveKind backs the per-file DELETE route
// for filestore-only kinds.
type FileStore interface {
	Put(id, kind, ext string, data []byte) (string, error)
	Get(id, kind, ext string) ([]byte, error)
	FindByKind(id, kind string) (string, error)
	OpenByRelPath(rel string) (*os.File, error)
	RemoveKind(id, kind string) error
	Remove(id string) error
}

// LLM proxies chat turns to the configured provider (llm-contract.md §6.3).
// nil means the proxy is disabled (no ANTHROPIC_API_KEY): /llm/info reports
// enabled=false and /llm/chat answers 503 llmDisabled.
type LLM interface {
	Chat(ctx context.Context, req protocol.LlmChatRequest) (protocol.LlmChatResponse, error)
	Model() string
}

// Deps bundles everything the API handlers require.
type Deps struct {
	Projects     ProjectStore
	Sessions     SessionStore
	Files        FileStore
	LiveSessions SessionCounter // optional: live edit-session connection counts (the hub)
	LLM          LLM            // optional: Anthropic proxy; nil = LLM routes disabled
	Bus          bus.Bus
	TokenTTL     time.Duration
	ProjectTTL   time.Duration // default project lifetime; 0 = no expiry (off)
	MaxBodyBytes int64
	AdminToken   string // when set, gates POST /auth/token
	// Spend controls for /llm/chat (llmlimit.go); 0 = unlimited.
	LLMRatePerMin  int // per-session turns per minute
	LLMMaxInFlight int // concurrent upstream calls, server-wide
	// Abuse guards for the non-LLM writes (ratelimit.go); 0 = off.
	AuthRatePerMin  int // POST /auth/token attempts per minute, per client IP
	WriteRatePerMin int // project creations + file uploads per minute, per session
}

// API holds the resolved dependencies and serves HTTP.
type API struct {
	deps      Deps
	llmRate   *llmRateLimiter
	llmGate   *llmGate
	authRate  *keyLimiter // per-IP, POST /auth/token
	writeRate *keyLimiter // per-session, project creation + file uploads
}

// nowMs is overridable in tests.
var nowMs = func() int64 { return time.Now().UnixMilli() }

// New constructs the API handler set.
func New(deps Deps) *API {
	if deps.MaxBodyBytes == 0 {
		deps.MaxBodyBytes = 32 << 20
	}
	if deps.TokenTTL == 0 {
		deps.TokenTTL = 7 * 24 * time.Hour
	}
	return &API{
		deps:      deps,
		llmRate:   newLLMRateLimiter(deps.LLMRatePerMin),
		llmGate:   newLLMGate(deps.LLMMaxInFlight),
		authRate:  newKeyLimiter(deps.AuthRatePerMin),
		writeRate: newKeyLimiter(deps.WriteRatePerMin),
	}
}

// Register mounts the REST routes onto mux. Project/file routes are gated by the
// auth middleware; POST /auth/token has its own admin gate.
func (a *API) Register(mux *http.ServeMux) {
	mux.HandleFunc("POST /auth/token", limitByIP(a.authRate, a.handleIssueToken))

	guard := auth.Middleware(a.deps.Sessions)
	protected := func(pattern string, h http.HandlerFunc) {
		mux.Handle(pattern, guard(h))
	}
	protected("GET /projects", a.handleListProjects)
	protected("POST /projects", limitBySession(a.writeRate, a.handleCreateProject))
	protected("GET /projects/{id}", a.handleGetProject)
	protected("PUT /projects/{id}", a.handleUpdateProject)
	protected("DELETE /projects/{id}", a.handleDeleteProject)
	protected("GET /projects/{id}/files/{kind}", a.handleGetFile)
	protected("POST /projects/{id}/files/{kind}", limitBySession(a.writeRate, a.handlePutFile))
	protected("DELETE /projects/{id}/files/{kind}", a.handleDeleteFile)
	protected("GET /llm/info", a.handleLLMInfo)
	protected("POST /llm/chat", a.handleLLMChat)
}

// Handler returns a ready ServeMux with all routes registered.
func (a *API) Handler() *http.ServeMux {
	mux := http.NewServeMux()
	a.Register(mux)
	return mux
}

// ----- shared response helpers -----

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, status int, code, msg string) {
	writeJSON(w, status, protocol.ErrorResponse{Code: code, Message: msg})
}

// decodeJSON reads a JSON body with a size cap and strict unknown-field
// rejection.
func (a *API) decodeJSON(w http.ResponseWriter, r *http.Request, dst any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, a.deps.MaxBodyBytes)
	dec := json.NewDecoder(r.Body)
	dec.DisallowUnknownFields()
	if err := dec.Decode(dst); err != nil {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "invalid JSON body: "+err.Error())
		return false
	}
	return true
}
