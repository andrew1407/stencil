package llm

// Why the proxy stays disabled; "" means it enables. main.go maps each reason
// onto its log line.
const (
	DisabledUnknownProvider = "unknownProvider" // LLM_PROVIDER is not one we can proxy to
	DisabledMissingKey      = "missingKey"      // the provider needs a key and none resolved
)

// Enablement decides whether the proxy enables for this configuration: an
// unknown provider is off, a key-requiring provider without a resolvable key
// (ResolveKey over LLM_API_KEY / ANTHROPIC_API_KEY) is off, anything else is on.
func Enablement(provider, apiKey, anthropicKey string) (enabled bool, reason string) {
	switch {
	case !KnownProvider(provider):
		return false, DisabledUnknownProvider
	case NeedsKey(provider) && ResolveKey(provider, apiKey, anthropicKey) == "":
		return false, DisabledMissingKey
	}
	return true, ""
}
