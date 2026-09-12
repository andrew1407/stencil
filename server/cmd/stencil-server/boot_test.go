package main

import (
	"strings"
	"testing"

	"stencil/server/internal/config"
	"stencil/server/internal/httpapi"
)

// FILESTORE_ROOT defaults to a relative path, which in a container with no
// mounted volume silently throws the bytes away on restart: boot must say so.
func TestFilestoreWarning(t *testing.T) {
	cases := []struct {
		root string
		warn bool
	}{
		{"./data/filestore", true}, // the default
		{"data/filestore", true},
		{"../shared/files", true},
		{"/var/lib/stencil/files", false},
		{"", false}, // never configured; filestore.New decides
	}
	for _, tc := range cases {
		got := filestoreWarning(tc.root)
		if (got != "") != tc.warn {
			t.Errorf("filestoreWarning(%q) = %q, want warning=%v", tc.root, got, tc.warn)
		}
		if tc.warn && !strings.Contains(got, tc.root) {
			t.Errorf("the warning should name the path: %q", got)
		}
	}
}

// The proxy stays off unless a provider is fully configured (503 llmDisabled).
func TestConfigureLLMStaysOffWithoutAKey(t *testing.T) {
	var deps httpapi.Deps
	configureLLM(config.Config{LLMProvider: "anthropic"}, &deps)
	if deps.LLM != nil {
		t.Fatal("a provider with no key must not enable the proxy")
	}
	configureLLM(config.Config{LLMProvider: "nonesuch", LLMAPIKey: "k"}, &deps)
	if deps.LLM != nil {
		t.Fatal("an unknown provider must not enable the proxy")
	}
	configureLLM(config.Config{LLMProvider: "ollama", LLMModel: "m"}, &deps)
	if deps.LLM == nil {
		t.Fatal("a keyless provider (ollama) should enable the proxy")
	}
}
