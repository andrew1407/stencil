package httpapi

// System-prompt pin: the proxy only forwards a system prompt that starts with
// one of Stencil's own heads, so a token can't turn the operator's key into a
// general LLM. The heads are not retyped here — assets/systemPrompt.json is a
// checked-in copy of browser/js/config/llm/systemPrompt.json (outside this Go
// module, so it can't be embedded directly), and llmprompt_test.go pins it.

import (
	_ "embed"
	"encoding/json"
	"strings"
)

// promptShapeMarker opens the line declaring the JSON response shape — the last
// line the two profiles share verbatim before their op bullets diverge.
const promptShapeMarker = `{"version":1,"reply":`

//go:embed assets/systemPrompt.json
var systemPromptAsset []byte

// The editor head is browser/js/llm/opPlan.js PROMPT_CORE_HEAD (byte-identical
// in desktop, cli, pystencil, mcp, bot and llm-contract.md §4); the extension
// head is extension/src/llm/opPlan.js PROMPT_CORE_HEAD (contract §8).
var llmEditorPromptHead, llmExtensionPromptHead = promptHeads()

// promptHeads cuts both heads out of the embedded asset. A corrupt asset is a
// build fault: failing here beats forwarding an unpinned prompt.
func promptHeads() (string, string) {
	var asset struct {
		Head          string `json:"head"`
		ExtensionHead string `json:"extensionHead"`
	}
	if err := json.Unmarshal(systemPromptAsset, &asset); err != nil {
		panic("httpapi: assets/systemPrompt.json: " + err.Error())
	}
	return promptPin(asset.Head), promptPin(asset.ExtensionHead)
}

// promptPin truncates a head after the response-shape line.
func promptPin(head string) string {
	i := strings.Index(head, promptShapeMarker)
	if i < 0 {
		panic("httpapi: systemPrompt.json head has no " + promptShapeMarker + " line")
	}
	nl := strings.IndexByte(head[i:], '\n')
	if nl < 0 {
		panic("httpapi: systemPrompt.json response-shape line is unterminated")
	}
	return head[:i+nl+1]
}

// hasStencilPromptHead reports whether a system prompt is recognizably one of
// Stencil's own (starts with a pinned head).
func hasStencilPromptHead(s string) bool {
	return strings.HasPrefix(s, llmEditorPromptHead) || strings.HasPrefix(s, llmExtensionPromptHead)
}
