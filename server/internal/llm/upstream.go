package llm

// Upstream failure classification (llm-contract.md §6.3, "Upstream failures say
// WHY"): a proxied call the upstream rejects must tell the user WHICH condition
// they can act on — out of credits, key rejected, unknown model, upstream rate
// limit, timeout, unreachable host. Error() keeps the full detail for the server
// log; ClientMessage() is the sanitized, bounded text a client may render.

import (
	"context"
	"errors"
	"fmt"
	"net"
	"regexp"
	"strings"
	"unicode"
)

// UpstreamKind is the small typed set of conditions the proxy distinguishes.
type UpstreamKind string

const (
	KindCredits    UpstreamKind = "credits"    // no credit balance / billing not set up
	KindAuth       UpstreamKind = "auth"       // key missing, invalid or revoked
	KindModel      UpstreamKind = "model"      // model unknown or not accessible
	KindRateLimit  UpstreamKind = "rateLimit"  // the UPSTREAM's limit, not this server's
	KindOverloaded UpstreamKind = "overloaded" // upstream 5xx / temporarily unavailable
	KindTimeout    UpstreamKind = "timeout"    // no reply within LLM_TIMEOUT_SECONDS
	KindNetwork    UpstreamKind = "network"    // DNS/dial failure, connection refused
	KindUnknown    UpstreamKind = "unknown"    // fall back to the upstream's own text
)

// maxUpstreamDetail bounds the upstream text echoed to clients (contract §6.3).
const maxUpstreamDetail = 200

// scanUpstreamDetail bounds how much raw upstream text the sanitizer touches at
// all — the body cap is 8 MiB and none of it is trusted.
const scanUpstreamDetail = 4 * maxUpstreamDetail

// reasons are the per-kind phrasings, in the terms the user thinks in. For a
// recognised kind the reason is the WHOLE client message (contract §6.3, "Say the
// reason once"), so each one is a single clause that reads on its own.
var reasons = map[UpstreamKind]string{
	KindCredits:    "the LLM provider is out of credits or has no active billing",
	KindAuth:       "the LLM provider rejected the API key",
	KindModel:      "the LLM provider does not have the requested model",
	KindRateLimit:  "the LLM provider is rate-limiting this server",
	KindOverloaded: "the LLM provider is temporarily unavailable",
	KindTimeout:    "the LLM provider did not respond in time",
	KindNetwork:    "the LLM provider is unreachable from this server",
	KindUnknown:    "the LLM provider returned an error",
}

// UpstreamError is a failure that came from (or on the way to) the upstream
// provider, as opposed to a bug in this server.
type UpstreamError struct {
	Kind     UpstreamKind
	Provider string
	Status   int    // upstream HTTP status, 0 for transport failures
	Type     string // the provider's own error type/code field, when it has one
	Detail   string // the upstream's message — untrusted, log-only until sanitized
	Err      error  // transport error, when the request never got a response

	// secret is the configured API key, kept only so ClientMessage can refuse to
	// echo upstream text containing it. Never formatted or logged.
	secret string
}

// Error is the server-log form: full upstream detail, never the key.
func (e *UpstreamError) Error() string {
	if e.Err != nil {
		return fmt.Sprintf("llm: %s request failed: %v", e.Provider, e.Err)
	}
	head := "llm: " + e.Provider
	if e.Type != "" {
		head += " " + e.Type
	}
	if e.Detail != "" {
		return fmt.Sprintf("%s (HTTP %d): %s", head, e.Status, e.Detail)
	}
	return fmt.Sprintf("%s HTTP %d", head, e.Status)
}

func (e *UpstreamError) Unwrap() error { return e.Err }

// ClientMessage is the sanitized, actionable reason clients render. A recognised
// condition is said ONCE — the bare reason, no status and none of the upstream's
// prose, which only restates it at four times the length. Only an unrecognised
// failure carries the upstream's own short text plus its status, since there the
// provider's words are the only information there is; that text is bounded,
// control-character free and secret-free.
func (e *UpstreamError) ClientMessage() string {
	if msg, ok := reasons[e.Kind]; ok && e.Kind != KindUnknown {
		return msg
	}
	msg := reasons[KindUnknown]
	if e.Status > 0 {
		msg += fmt.Sprintf(" (HTTP %d)", e.Status)
	}
	// Transport errors carry the request URL, so their text is log-only.
	if e.Err == nil {
		if detail := sanitizeUpstreamText(e.Detail, e.secret); detail != "" {
			msg += ": " + detail
		}
	}
	return msg
}

// secretish matches token-shaped runs so a key (or a base64 payload) echoed back
// by an upstream never reaches a client, whatever vendor's format it is in.
// The separators are punctuation (or "bearer ") so ordinary prose — "Incorrect
// API key provided" — survives intact.
var secretish = regexp.MustCompile(`(?i)(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}` +
	`|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}` +
	`|[A-Za-z0-9_-]{24,}`)

// urlish matches absolute URLs — internal endpoints are not the client's business.
var urlish = regexp.MustCompile(`(?i)[a-z][a-z0-9+.-]*://\S+`)

// sanitizeUpstreamText turns untrusted upstream text into something safe to put
// in an error message: control characters out, URLs and token-shaped runs
// redacted, collapsed whitespace, at most maxUpstreamDetail characters. Returns
// "" when any fragment of secret survives — better silent than leaking a key.
func sanitizeUpstreamText(text, secret string) string {
	if text == "" {
		return ""
	}
	if r := []rune(text); len(r) > scanUpstreamDetail {
		text = string(r[:scanUpstreamDetail])
	}
	// Newlines, control and other non-printing runes out: the message must not be
	// able to forge extra lines of server output.
	text = strings.Map(func(r rune) rune {
		if !unicode.IsPrint(r) {
			return ' '
		}
		return r
	}, text)
	text = urlish.ReplaceAllString(text, "[redacted]")
	text = secretish.ReplaceAllString(text, "[redacted]")
	text = strings.Join(strings.Fields(text), " ")
	if r := []rune(text); len(r) > maxUpstreamDetail {
		text = strings.TrimSpace(string(r[:maxUpstreamDetail-1])) + "…"
	}
	if containsSecretFragment(text, secret) {
		return ""
	}
	return text
}

// containsSecretFragment reports whether text shows any 8-character run of the
// configured key — a partial key is still a leak.
func containsSecretFragment(text, secret string) bool {
	const frag = 8
	if len(secret) < frag {
		return false
	}
	for i := 0; i+frag <= len(secret); i++ {
		if strings.Contains(text, secret[i:i+frag]) {
			return true
		}
	}
	return false
}

// classifyUpstream maps an upstream reply onto a kind. The provider's own error
// type/code field and the HTTP status are checked before any string matching, so
// this is not Anthropic-specific: Anthropic sends {error:{type}}, OpenAI-compat
// sends {error:{type,code}}, Ollama sends only {"error":"…"} plus a status.
func classifyUpstream(status int, errType, message string) UpstreamKind {
	t := strings.ToLower(errType)
	m := strings.ToLower(message)
	switch {
	// Billing first: a spent balance arrives as a 400 or a 429 depending on vendor.
	case hasAny(t, "insufficient_quota", "billing", "credit"),
		hasAny(m, "credit balance", "insufficient_quota", "insufficient quota", "purchase credits", "billing", "out of credits"),
		status == 402:
		return KindCredits
	case hasAny(t, "authentication", "invalid_api_key", "permission", "unauthorized", "forbidden"),
		status == 401, status == 403:
		return KindAuth
	case hasAny(t, "model_not_found", "not_found"),
		hasAny(m, "model not found", "unknown model", "does not exist", "try pulling", "no such model"),
		status == 404:
		return KindModel
	case hasAny(t, "rate_limit"), status == 429:
		return KindRateLimit
	case status == 408, status == 504:
		return KindTimeout
	case hasAny(t, "overloaded", "api_error"), status >= 500:
		return KindOverloaded
	}
	return KindUnknown
}

// classifyTransport maps a request that never got a reply.
func classifyTransport(err error) UpstreamKind {
	if errors.Is(err, context.DeadlineExceeded) {
		return KindTimeout
	}
	var ne net.Error
	if errors.As(err, &ne) && ne.Timeout() {
		return KindTimeout
	}
	return KindNetwork
}

func hasAny(s string, subs ...string) bool {
	for _, sub := range subs {
		if strings.Contains(s, sub) {
			return true
		}
	}
	return false
}

// statusErr builds the classified error for a non-2xx upstream reply. errType is
// the provider's type/code field(s), empty when it has none.
func (c *Client) statusErr(status int, errType, detail string) *UpstreamError {
	return &UpstreamError{
		Kind: classifyUpstream(status, errType, detail), Provider: c.provider,
		Status: status, Type: errType, Detail: detail, secret: c.key,
	}
}

// transportErr builds the classified error for a request that never landed.
func (c *Client) transportErr(err error) *UpstreamError {
	return &UpstreamError{
		Kind: classifyTransport(err), Provider: c.provider, Err: err, secret: c.key,
	}
}
