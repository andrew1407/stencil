package llm

// Scrubbing upstream prose before a client sees it. LLM_BASE_URL is operator
// config, so the text on the other end is not ours: it may echo the key back, or
// name internal hosts. Nothing leaves here unredacted (sanitize_test.go).

import (
	"regexp"
	"strings"
	"unicode"
)

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
