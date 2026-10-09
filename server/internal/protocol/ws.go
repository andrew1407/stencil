package protocol

// ----- WebSocket envelope -----

// WS message type identifiers.
const (
	// client -> server
	WSHello = "hello" // MUST be the first frame: { token, clientId, name? }
	WSPing  = "ping"

	// server -> client
	WSError     = "error" // { code, message }
	WSPong      = "pong"
	WSProjectEv = "project-event" // global /events feed: { event, project (metadata) }
)

// Project-event sub-types for the global /events feed.
const (
	EventCreated = "created"
	EventUpdated = "updated"
	EventDeleted = "deleted"
)

// Error codes carried in WSMessage.Code and ErrorResponse.Code.
const (
	CodeUnauthorized = "unauthorized" // a bad hello token, or a live connection's token expiring
	CodeNotFound     = "notFound"
	CodeConflict     = "conflict"
	CodeBadRequest   = "badRequest" // REST, and a hello that names a project or overruns a field cap
	CodeInternal     = "internal"
	CodeLlmDisabled  = "llmDisabled"  // /llm/chat on a server with no LLM key configured
	CodeRateLimited  = "rateLimited"  // over a rate or capacity (/llm/chat, /auth/token, writes, hello, connections per IP)
	CodeShutdown     = "shuttingDown" // the server is closing every connection
	CodeLlmUpstream  = "llmUpstream"  // /llm/chat when the upstream provider failed; message names the condition
)

// WSMessage is the JSON envelope for every text frame in both directions. Fields
// are optional per message type; Type selects which are meaningful.
type WSMessage struct {
	Type string `json:"type"`

	// auth / identity (hello). ProjectID is refused: the feed is the only thing a connection serves, and the
	// field stays so an older client's hello is answered with a reason rather than dropped.
	Token     string `json:"token,omitempty"`
	ClientID  string `json:"clientId,omitempty"`
	Name      string `json:"name,omitempty"`
	ProjectID string `json:"projectId,omitempty"`

	// events
	Project *ProjectRecord `json:"project,omitempty"`
	Event   string         `json:"event,omitempty"`

	// errors
	Code    string `json:"code,omitempty"`
	Message string `json:"message,omitempty"`
}
