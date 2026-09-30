// LLM_MODEL / LLM_MAX_TOKENS defaults: the serverDefaults block of the canonical
// common/config/llm/providers.json, embedded through assets/providers.json, a generated
// byte-equal copy (go:embed cannot leave this module) that llmdefaults_test.go pins.
package config

import (
	_ "embed"
	"encoding/json"
)

//go:generate go run ../tools/syncassets ../../../common/config/llm/providers.json assets/providers.json
//go:embed assets/providers.json
var providersAsset []byte

// llmServerDefaults is providers.json's serverDefaults block.
type llmServerDefaults struct {
	Model     string `json:"model"`
	MaxTokens int    `json:"maxTokens"`
}

var serverLLMDefaults = mustServerDefaults(providersAsset)

func mustServerDefaults(raw []byte) llmServerDefaults {
	var doc struct {
		ServerDefaults llmServerDefaults `json:"serverDefaults"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil || doc.ServerDefaults.Model == "" || doc.ServerDefaults.MaxTokens <= 0 {
		panic("config: assets/providers.json carries no usable serverDefaults")
	}
	return doc.ServerDefaults
}
