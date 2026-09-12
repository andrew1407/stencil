package llm

// Who the proxy can talk to: the provider ids, their defaults and credentials,
// and the table of wire mappings (llm-contract.md §6). protocol.LlmChatRequest/
// Response is canonical for all of them — only the wire shape differs (cf.
// browser llmClient.js).

// Provider identifiers (the §5 values the clients use, minus the client-only
// "stencil-server" — from this process's side that IS the upstream choice).
const (
	ProviderAnthropic = "anthropic"
	ProviderOllama    = "ollama"
	ProviderOpenAI    = "openai-compat"
)

var mappings = map[string]providerMapping{
	ProviderAnthropic: anthropicMapping{},
	ProviderOllama:    ollamaMapping{},
	ProviderOpenAI:    openAIMapping{},
}

// mappingFor resolves a provider id; an unknown one falls back to Anthropic, as
// the provider switch it replaced did.
func mappingFor(provider string) providerMapping {
	if m, ok := mappings[provider]; ok {
		return m
	}
	return mappings[ProviderAnthropic]
}

// KnownProvider reports whether p is one this process can proxy to. Empty
// counts as Anthropic (the historical default).
func KnownProvider(p string) bool {
	switch p {
	case "", ProviderAnthropic, ProviderOllama, ProviderOpenAI:
		return true
	}
	return false
}

// DefaultBaseURL is the per-provider endpoint used when LLM_BASE_URL is unset —
// the same defaults table every client ships (contract §5).
func DefaultBaseURL(provider string) string {
	switch provider {
	case ProviderOllama:
		return "http://localhost:11434"
	case ProviderOpenAI:
		return "http://localhost:1234/v1"
	default:
		return "https://api.anthropic.com"
	}
}

// NeedsKey reports whether a provider requires an API key to be usable. Local
// servers (Ollama, LM Studio, llama.cpp) need none; Anthropic always does.
func NeedsKey(provider string) bool {
	return provider == "" || provider == ProviderAnthropic
}

// ResolveKey picks the credential for provider. LLM_API_KEY (apiKey) is
// provider-agnostic; ANTHROPIC_API_KEY (anthropicKey) is honoured ONLY for
// Anthropic — an operator who named a key for one vendor must never have it put
// on the wire to a third-party OpenAI-compatible host by a provider switch.
func ResolveKey(provider, apiKey, anthropicKey string) string {
	if apiKey != "" {
		return apiKey
	}
	if NeedsKey(provider) {
		return anthropicKey
	}
	return ""
}
