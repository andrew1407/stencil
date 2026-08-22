package llm

import "testing"

// The truth table main.go's proxy switch relies on: unknown providers stay off,
// key-requiring providers without a resolvable key stay off, everything else
// enables — with ANTHROPIC_API_KEY honoured for Anthropic only.
func TestEnablement(t *testing.T) {
	cases := []struct {
		name                           string
		provider, apiKey, anthropicKey string
		wantEnabled                    bool
		wantReason                     string
	}{
		{"unknown provider", "bogus", "", "", false, DisabledUnknownProvider},
		{"unknown provider with keys", "bogus", "k", "a", false, DisabledUnknownProvider},
		{"anthropic without key", ProviderAnthropic, "", "", false, DisabledMissingKey},
		{"anthropic with LLM_API_KEY", ProviderAnthropic, "k", "", true, ""},
		{"anthropic with legacy ANTHROPIC_API_KEY", ProviderAnthropic, "", "a", true, ""},
		{"empty provider defaults to anthropic, no key", "", "", "", false, DisabledMissingKey},
		{"empty provider with legacy key", "", "", "a", true, ""},
		{"ollama needs no key", ProviderOllama, "", "", true, ""},
		{"ollama with a key still on", ProviderOllama, "k", "", true, ""},
		{"openai-compat needs no key", ProviderOpenAI, "", "", true, ""},
		{"openai-compat with a key", ProviderOpenAI, "k", "", true, ""},
		// The legacy Anthropic key never gates (nor reaches) a third-party host.
		{"openai-compat with only the anthropic key", ProviderOpenAI, "", "a", true, ""},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			enabled, reason := Enablement(c.provider, c.apiKey, c.anthropicKey)
			if enabled != c.wantEnabled || reason != c.wantReason {
				t.Fatalf("Enablement(%q,%q,%q) = (%v,%q), want (%v,%q)",
					c.provider, c.apiKey, c.anthropicKey, enabled, reason, c.wantEnabled, c.wantReason)
			}
		})
	}
}
