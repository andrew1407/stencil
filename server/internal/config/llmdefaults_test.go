package config

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

// Drift check against the canonical browser/js/config/llm/providers.json: the embedded copy stays
// byte-identical, so LLM_MODEL's default can only change there.
func TestProvidersAssetPinsCanonical(t *testing.T) {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	canonical := filepath.Join(filepath.Dir(thisFile), "..", "..", "..", "browser", "js", "config", "llm", "providers.json")
	raw, err := os.ReadFile(canonical)
	if os.IsNotExist(err) {
		t.Skipf("canonical %s not present (standalone server checkout)", canonical)
	}
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(raw, providersAsset) {
		t.Errorf("assets/providers.json differs from %s — re-copy the canonical file", canonical)
	}
}

// The defaults Load falls back on are the asset's, not a retyped literal.
func TestLLMDefaultsComeFromTheAsset(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	for _, k := range []string{"LLM_MODEL", "LLM_MAX_TOKENS"} {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.LLMModel != serverLLMDefaults.Model || cfg.LLMMaxTokens != serverLLMDefaults.MaxTokens {
		t.Fatalf("defaults %q/%d, asset says %+v", cfg.LLMModel, cfg.LLMMaxTokens, serverLLMDefaults)
	}
}
