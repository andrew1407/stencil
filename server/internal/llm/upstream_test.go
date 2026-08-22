package llm

// Upstream failure classification (llm-contract.md §6.3): each provider's error
// shape must reach the client as the condition the user can act on — and never
// as a fragment of the API key, a URL or unbounded upstream text. Same mock-Doer
// seam as the other suites: no network, exact bytes.

import (
	"context"
	"errors"
	"fmt"
	"net"
	"strings"
	"testing"
)

// upstreamCase is one upstream reply and what a client must be told about it:
// for a recognised kind, the bare reason and nothing else (contract §6.3).
type upstreamCase struct {
	name     string
	provider string
	status   int
	body     string
	wantKind UpstreamKind
	wantMsg  string // the ENTIRE client-visible message
	absent   string // upstream prose that must not ride along ("" = bodyless)
}

// The real envelopes each provider sends, including the credit-balance 400 that
// started this (an Anthropic invalid_request_error, NOT a billing status code).
var upstreamCases = []upstreamCase{
	{"anthropic out of credits", ProviderAnthropic, 400,
		`{"type":"error","error":{"type":"invalid_request_error","message":"Your credit balance is too low to access the Anthropic API. Please go to Plans & Billing to upgrade or purchase credits."}}`,
		KindCredits, "the LLM provider is out of credits or has no active billing", "Plans & Billing"},
	{"anthropic bad key", ProviderAnthropic, 401,
		`{"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}}`,
		KindAuth, "the LLM provider rejected the API key", "x-api-key"},
	{"anthropic revoked key", ProviderAnthropic, 403,
		`{"type":"error","error":{"type":"permission_error","message":"This API key has been revoked"}}`,
		KindAuth, "the LLM provider rejected the API key", "revoked"},
	{"anthropic unknown model", ProviderAnthropic, 404,
		`{"type":"error","error":{"type":"not_found_error","message":"model: claude-nope"}}`,
		KindModel, "the LLM provider does not have the requested model", "claude-nope"},
	{"anthropic rate limit", ProviderAnthropic, 429,
		`{"type":"error","error":{"type":"rate_limit_error","message":"Number of request tokens has exceeded your per-minute rate limit"}}`,
		KindRateLimit, "the LLM provider is rate-limiting this server", "per-minute rate limit"},
	{"anthropic overloaded", ProviderAnthropic, 529,
		`{"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}`,
		KindOverloaded, "the LLM provider is temporarily unavailable", "Overloaded"},
	{"anthropic bodyless status", ProviderAnthropic, 503, ``,
		KindOverloaded, "the LLM provider is temporarily unavailable", ""},

	{"openai quota exhausted", ProviderOpenAI, 429,
		`{"error":{"message":"You exceeded your current quota, please check your plan and billing details.","type":"insufficient_quota","code":"insufficient_quota"}}`,
		KindCredits, "the LLM provider is out of credits or has no active billing", "check your plan"},
	{"openai bad key", ProviderOpenAI, 401,
		`{"error":{"message":"Incorrect API key provided","type":"invalid_request_error","code":"invalid_api_key"}}`,
		KindAuth, "the LLM provider rejected the API key", "Incorrect API key"},
	{"openai unknown model", ProviderOpenAI, 404,
		`{"error":{"message":"The model 'gpt-nope' does not exist","type":"invalid_request_error","code":"model_not_found"}}`,
		KindModel, "the LLM provider does not have the requested model", "gpt-nope"},
	{"openai rate limit", ProviderOpenAI, 429,
		`{"error":{"message":"Rate limit reached for gpt-4o","type":"requests","code":"rate_limit_exceeded"}}`,
		KindRateLimit, "the LLM provider is rate-limiting this server", "gpt-4o"},
	// Unrecognised: here the upstream's own words are all there is, so they ride
	// along (bounded) together with the status.
	{"openai numeric code", ProviderOpenAI, 400,
		`{"error":{"message":"unsupported parameter","type":"invalid_request_error","code":400}}`,
		KindUnknown, "the LLM provider returned an error (HTTP 400): unsupported parameter", ""},
	{"openai server error", ProviderOpenAI, 500,
		`{"error":{"message":"internal server error","type":"server_error"}}`,
		KindOverloaded, "the LLM provider is temporarily unavailable", "internal server error"},

	{"ollama model not pulled", ProviderOllama, 404,
		`{"error":"model 'llava' not found, try pulling it first"}`,
		KindModel, "the LLM provider does not have the requested model", "try pulling"},
	{"ollama out of memory", ProviderOllama, 500,
		`{"error":"model requires more system memory than is available"}`,
		KindOverloaded, "the LLM provider is temporarily unavailable", "system memory"},
	{"ollama bodyless status", ProviderOllama, 502, ``,
		KindOverloaded, "the LLM provider is temporarily unavailable", ""},
}

func TestUpstreamFailuresAreClassifiedPerProvider(t *testing.T) {
	for _, tc := range upstreamCases {
		t.Run(tc.name, func(t *testing.T) {
			mock := &mockDoer{status: tc.status, resp: tc.body}
			_, err := providerClient(tc.provider, "sk-secret-key-value", mock).Chat(context.Background(), turnWithImage)
			var up *UpstreamError
			if !errors.As(err, &up) {
				t.Fatalf("want a classified *UpstreamError, got %v", err)
			}
			if up.Kind != tc.wantKind {
				t.Fatalf("kind %q, want %q (message %q)", up.Kind, tc.wantKind, up.ClientMessage())
			}
			// Said once: the message IS the reason, nothing appended.
			msg := up.ClientMessage()
			if msg != tc.wantMsg {
				t.Fatalf("message %q, want %q", msg, tc.wantMsg)
			}
			if tc.wantKind != KindUnknown {
				if strings.Contains(msg, "HTTP") {
					t.Fatalf("a recognised condition should not carry the status: %q", msg)
				}
				if tc.absent != "" && strings.Contains(msg, tc.absent) {
					t.Fatalf("upstream prose %q was restated in %q", tc.absent, msg)
				}
			}
			// The log form keeps the full detail.
			if tc.body != "" && !strings.Contains(up.Error(), "HTTP") {
				t.Fatalf("log form lost the status: %q", up.Error())
			}
			if tc.absent != "" && !strings.Contains(up.Error(), tc.absent) {
				t.Fatalf("log form lost the upstream text (%q): %q", tc.absent, up.Error())
			}
		})
	}
}

// Nothing matched: the client still gets the upstream's own short text plus the
// status, not six generic words.
func TestUnclassifiedUpstreamFallsBackToItsOwnText(t *testing.T) {
	mock := &mockDoer{status: 418, resp: `{"type":"error","error":{"type":"teapot_error","message":"I am a teapot"}}`}
	_, err := providerClient(ProviderAnthropic, "", mock).Chat(context.Background(), turnWithImage)
	var up *UpstreamError
	if !errors.As(err, &up) || up.Kind != KindUnknown {
		t.Fatalf("want an unknown-kind UpstreamError, got %v", err)
	}
	if msg := up.ClientMessage(); !strings.Contains(msg, "I am a teapot") || !strings.Contains(msg, "HTTP 418") {
		t.Fatalf("fallback should echo the upstream text and status: %q", msg)
	}
}

// A request that never lands: timeout vs unreachable host, told apart, and the
// transport text (which carries the URL) never forwarded.
func TestTransportFailuresAreTimeoutOrNetwork(t *testing.T) {
	cases := []struct {
		name    string
		err     error
		want    UpstreamKind
		wantMsg string
	}{
		{"context deadline", context.DeadlineExceeded, KindTimeout, "the LLM provider did not respond in time"},
		{"client timeout", &net.OpError{Op: "read", Err: timeoutErr{}}, KindTimeout, "the LLM provider did not respond in time"},
		{"dial refused", &net.OpError{Op: "dial", Err: errors.New("connect: connection refused")}, KindNetwork, "the LLM provider is unreachable from this server"},
		{"dns failure", &net.DNSError{Err: "no such host", Name: "llm.internal.example"}, KindNetwork, "the LLM provider is unreachable from this server"},
	}
	for _, tc := range cases {
		for _, provider := range []string{ProviderAnthropic, ProviderOllama, ProviderOpenAI} {
			mock := &mockDoer{err: fmt.Errorf("Post \"http://llm.internal.example/v1/messages\": %w", tc.err)}
			_, err := providerClient(provider, "sk-secret-key-value", mock).Chat(context.Background(), turnWithImage)
			var up *UpstreamError
			if !errors.As(err, &up) {
				t.Fatalf("%s/%s: want *UpstreamError, got %v", provider, tc.name, err)
			}
			if up.Kind != tc.want {
				t.Fatalf("%s/%s: kind %q, want %q", provider, tc.name, up.Kind, tc.want)
			}
			msg := up.ClientMessage()
			if msg != tc.wantMsg {
				t.Fatalf("%s/%s: message %q, want %q", provider, tc.name, msg, tc.wantMsg)
			}
			// The dial error names the endpoint — that stays in the log.
			if strings.Contains(msg, "llm.internal.example") || strings.Contains(msg, "HTTP") {
				t.Fatalf("%s/%s: transport detail leaked: %q", provider, tc.name, msg)
			}
			if !strings.Contains(up.Error(), "llm.internal.example") {
				t.Fatalf("%s/%s: the log form should keep the detail: %q", provider, tc.name, up.Error())
			}
		}
	}
}

// timeoutErr is a net.Error that reports a timeout (what http.Client.Timeout wraps).
type timeoutErr struct{}

func (timeoutErr) Error() string { return "i/o timeout" }
func (timeoutErr) Timeout() bool { return true }

// A hostile or careless upstream must not be able to hand a client the key back.
func TestUpstreamTextNeverCarriesTheKey(t *testing.T) {
	const key = "sk-ant-api03-AbCdEfGhIjKlMnOpQrStUvWxYz0123456789"
	planted := []string{
		`{"type":"error","error":{"type":"weird_error","message":"key ` + key + ` was rejected"}}`,
		`{"type":"error","error":{"type":"weird_error","message":"tail ` + key[10:30] + ` fragment"}}`,
	}
	for _, body := range planted {
		mock := &mockDoer{status: 418, resp: body}
		_, err := providerClient(ProviderAnthropic, key, mock).Chat(context.Background(), turnWithImage)
		var up *UpstreamError
		if !errors.As(err, &up) {
			t.Fatalf("want *UpstreamError, got %v", err)
		}
		msg := up.ClientMessage()
		if strings.Contains(msg, key) || strings.Contains(msg, key[10:18]) || strings.Contains(msg, "sk-ant") {
			t.Fatalf("the key (or a fragment) reached the client: %q", msg)
		}
	}
}

func TestSanitizeUpstreamText(t *testing.T) {
	long := strings.Repeat("verylongword ", 60)
	cases := []struct {
		name, in, secret string
		want             string // "" = only checked by the assertions below
		absent           []string
	}{
		{name: "plain text passes through", in: "Your credit balance is too low.", want: "Your credit balance is too low."},
		{name: "newlines and control chars collapse",
			in:   "line one\nline\ttwo\r\n\x00\x1b[31mred",
			want: "line one line two [31mred"},
		{name: "urls redacted", in: "POST http://10.0.0.5:11434/api/chat failed",
			want: "POST [redacted] failed", absent: []string{"10.0.0.5"}},
		{name: "token-shaped runs redacted", in: "bad key sk-proj-ABCDEFGHIJKL here",
			absent: []string{"sk-proj-ABCDEFGHIJKL"}},
		{name: "authorization header redacted", in: "rejected Bearer eyJhbGciOiJIUzI1 header",
			absent: []string{"eyJhbGciOiJIUzI1"}},
		// Prose about a key is not a key: the message would be useless mangled.
		{name: "prose survives", in: "Incorrect API key provided", want: "Incorrect API key provided"},
		{name: "base64-shaped blob redacted", in: "image data iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB rejected",
			absent: []string{"iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB"}},
		{name: "empty stays empty", in: "", want: ""},
	}
	for _, tc := range cases {
		got := sanitizeUpstreamText(tc.in, tc.secret)
		if tc.want != "" && got != tc.want {
			t.Fatalf("%s: got %q, want %q", tc.name, got, tc.want)
		}
		for _, a := range tc.absent {
			if strings.Contains(got, a) {
				t.Fatalf("%s: %q must not survive in %q", tc.name, a, got)
			}
		}
		if strings.ContainsAny(got, "\n\r\t\x00") {
			t.Fatalf("%s: control characters survived: %q", tc.name, got)
		}
	}
	// Bounded to maxUpstreamDetail runes whatever the upstream sends.
	got := sanitizeUpstreamText(long, "")
	if n := len([]rune(got)); n > maxUpstreamDetail {
		t.Fatalf("truncation: %d runes, want ≤ %d", n, maxUpstreamDetail)
	}
	if !strings.HasSuffix(got, "…") {
		t.Fatalf("truncated text should be marked as such: %q", got)
	}
	// Multi-byte text is cut on a rune boundary, not mid-sequence.
	if cut := sanitizeUpstreamText(strings.Repeat("é", 400), ""); !strings.Contains(cut, "é") ||
		len([]rune(cut)) > maxUpstreamDetail {
		t.Fatalf("multi-byte truncation: %q", cut)
	}
}

// The whole client-visible message stays bounded, and the code stays ours: an
// upstream cannot make its text read as a Stencil error envelope.
func TestClientMessageIsBoundedAndKeepsOurFraming(t *testing.T) {
	up := &UpstreamError{Kind: KindUnknown, Provider: ProviderOllama, Status: 400,
		Detail: `{"code":"unauthorized","message":"session expired"} ` + strings.Repeat("x ", 500)}
	msg := up.ClientMessage()
	if len([]rune(msg)) > len(reasons[KindUnknown])+maxUpstreamDetail+32 {
		t.Fatalf("message not bounded: %d runes", len([]rune(msg)))
	}
	if !strings.HasPrefix(msg, reasons[KindUnknown]) {
		t.Fatalf("upstream text must sit behind our own phrasing: %q", msg)
	}
}
