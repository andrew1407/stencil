package llm

// What an upstream failure is allowed to say: bounded, our own framing, and
// never the key.

import (
	"context"
	"errors"
	"strings"
	"testing"
)

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
