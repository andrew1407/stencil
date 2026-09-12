// LLM proxy settings (llm-contract.md §6): the LLM_* vars plus the legacy
// ANTHROPIC_API_KEY, split out so config.go stays the list of tunables.
package config

import "time"

func loadLLM(get getter, cfg *Config) error {
	cfg.LLMProvider = get("LLM_PROVIDER", "anthropic")
	cfg.LLMAPIKey = get("LLM_API_KEY", "")
	cfg.AnthropicKey = get("ANTHROPIC_API_KEY", "")
	cfg.LLMModel = get("LLM_MODEL", defaultLLMModel)
	cfg.LLMBaseURL = get("LLM_BASE_URL", "") // "" = the provider's default

	var err error
	if cfg.LLMMaxTokens, err = positiveInt(get, "LLM_MAX_TOKENS", defaultLLMMaxTokens, 1); err != nil {
		return err
	}
	seconds, err := positiveInt(get, "LLM_TIMEOUT_SECONDS", int(defaultLLMTimeout/time.Second), 1)
	if err != nil {
		return err
	}
	cfg.LLMTimeout = time.Duration(seconds) * time.Second
	if cfg.LLMRatePerMin, err = positiveInt(get, "LLM_RATE_PER_MINUTE", defaultLLMRatePerMin, 0); err != nil {
		return err
	}
	cfg.LLMMaxInFlight, err = positiveInt(get, "LLM_MAX_IN_FLIGHT", defaultLLMMaxInFlight, 0)
	return err
}
