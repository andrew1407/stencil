package protocol

import "encoding/json"

// ----- WebSocket envelope -----

// WS message type identifiers.
const (
	// client -> server
	WSHello     = "hello"     // MUST be the first frame: { token, clientId, name? }
	WSSubscribe = "subscribe" // request a full snapshot (no incremental resync)
	WSEdit      = "edit"      // { version, op, payload } layout mutation
	WSCursor    = "cursor"    // { x, y } ephemeral, relayed not persisted
	WSPresence  = "presence"  // { state } ephemeral
	WSSave      = "save"      // commit layout -> store
	WSPing      = "ping"

	// server -> client
	WSWelcome   = "welcome"   // { project, layout, version, peers[] }
	WSPeerJoin  = "peer-join" // { clientId, name }
	WSPeerLeave = "peer-leave"
	WSSynced    = "synced" // { version, resultPath? } ack/commit confirmation
	WSError     = "error"  // { code, message }
	WSPong      = "pong"
	WSProjectEv = "project-event" // global /events feed: { event, project }
)

// Project-event sub-types for the global /events feed.
const (
	EventCreated = "created"
	EventUpdated = "updated"
	EventDeleted = "deleted"
)

// Error codes carried in WSMessage.Code and ErrorResponse.Code.
const (
	CodeUnauthorized = "unauthorized"
	CodeBadVersion   = "badVersion"
	CodeNotFound     = "notFound"
	CodeConflict     = "conflict"
	CodeBadRequest   = "badRequest"
	CodeInternal     = "internal"
	CodeLlmDisabled  = "llmDisabled"  // /llm/chat on a server with no LLM key configured
	CodeRateLimited  = "rateLimited"  // over a rate or in-flight cap (/llm/chat, /auth/token, writes, hello)
	CodeShutdown     = "shuttingDown" // the server is closing sessions: unsaved live edits are lost
	CodeLlmUpstream  = "llmUpstream"  // /llm/chat when the upstream provider failed; message names the condition
)

// Peer identifies a participant in a live edit session.
type Peer struct {
	ClientID string `json:"clientId"`
	Name     string `json:"name,omitempty"`
}

// WSMessage is the JSON envelope for every text frame in both directions. Fields
// are optional per message type; Type selects which are meaningful.
type WSMessage struct {
	Type string `json:"type"`

	// auth / identity (hello). ProjectID routes a hello frame to a project session; empty selects the global
	// /events feed, carrying the route over transports (TCP) that have no request path.
	Token     string `json:"token,omitempty"`
	ClientID  string `json:"clientId,omitempty"`
	Name      string `json:"name,omitempty"`
	ProjectID string `json:"projectId,omitempty"`

	// edit / sync
	Version int64           `json:"version,omitempty"`
	Op      string          `json:"op,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`

	// snapshots / events
	Project    *ProjectRecord  `json:"project,omitempty"`
	Layout     json.RawMessage `json:"layout,omitempty"`
	Peers      []Peer          `json:"peers,omitempty"`
	Event      string          `json:"event,omitempty"`
	ResultPath string          `json:"resultPath,omitempty"`

	// relay bookkeeping
	FromClientID string `json:"fromClientId,omitempty"`

	// presence / cursor
	X     float64 `json:"x,omitempty"`
	Y     float64 `json:"y,omitempty"`
	State string  `json:"state,omitempty"`

	// errors
	Code    string `json:"code,omitempty"`
	Message string `json:"message,omitempty"`
}
