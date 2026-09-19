package httpapi

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// Drift check against canonical browser/js/config/llm/systemPrompt.json: the embedded copy must stay
// byte-identical and the heads exact prefixes. go:embed can't cross the module boundary, so it is read here.
func TestPromptHeadsPinCanonicalAsset(t *testing.T) {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	repoRoot := filepath.Join(filepath.Dir(thisFile), "..", "..", "..")
	assetPath := filepath.Join(repoRoot, "browser", "js", "config", "llm", "systemPrompt.json")

	raw, err := os.ReadFile(assetPath)
	if os.IsNotExist(err) {
		t.Skipf("canonical asset %s not present (standalone server checkout); "+
			"browser/tests/systemPromptAsset.test.js is the mirror canary", assetPath)
	}
	if err != nil {
		t.Fatalf("read canonical asset: %v", err)
	}

	if !bytes.Equal(raw, systemPromptAsset) {
		t.Errorf("assets/systemPrompt.json differs from %s (%d vs %d bytes) — "+
			"re-copy the canonical asset", assetPath, len(systemPromptAsset), len(raw))
	}

	var asset struct {
		Head          string `json:"head"`
		ExtensionHead string `json:"extensionHead"`
	}
	if err := json.Unmarshal(raw, &asset); err != nil {
		t.Fatalf("parse %s: %v", assetPath, err)
	}

	if got := len(asset.Head); got != 1197 {
		t.Errorf("asset head is %d bytes, want 1197", got)
	}
	if got := len(asset.ExtensionHead); got != 656 {
		t.Errorf("asset extensionHead is %d bytes, want 656", got)
	}
	if got := len(llmEditorPromptHead); got != 328 {
		t.Errorf("llmEditorPromptHead is %d bytes, want 328", got)
	}
	if got := len(llmExtensionPromptHead); got != 434 {
		t.Errorf("llmExtensionPromptHead is %d bytes, want 434", got)
	}
	if !strings.HasPrefix(asset.Head, llmEditorPromptHead) {
		t.Error("llmEditorPromptHead is not a byte prefix of the asset's head")
	}
	if !strings.HasPrefix(asset.ExtensionHead, llmExtensionPromptHead) {
		t.Error("llmExtensionPromptHead is not a byte prefix of the asset's extensionHead")
	}
}
