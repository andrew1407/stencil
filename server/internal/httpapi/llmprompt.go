package httpapi

import "strings"

// System-prompt pin: the proxy only forwards a system prompt that starts with
// one of Stencil's own byte-pinned heads, so a token can't turn the operator's
// key into a general LLM. Canonical source: browser/js/config/llm/systemPrompt.json
// (keys head/extensionHead); llmprompt_test.go asserts the pins against it.
const (
	// maxLLMSystemBytes bounds the system prompt alone; a real prompt
	// (head + op bullets + tail + short dynamic suffix) sits far below it.
	maxLLMSystemBytes = 32 * 1024

	// llmEditorPromptHead is browser/js/llm/opPlan.js PROMPT_CORE_HEAD, byte-identical
	// in desktop, cli, pystencil, mcp, bot, and llm-contract.md §4.
	llmEditorPromptHead = `You are the AI assistant inside Stencil, an image-annotation tool. You help the user
edit the working image by planning operations; you never produce image data yourself.

Respond with EXACTLY ONE JSON object and no other text, in this shape:
{"version":1,"reply":"<short answer for the user>","actions":[...],"variants":[...]}
`

	// llmExtensionPromptHead is extension/src/llm/opPlan.js PROMPT_CORE_HEAD
	// (the contract-§8 extension profile; deliberately diverged from the editor's).
	llmExtensionPromptHead = `You are the AI assistant inside Stencil, an image-annotation tool. You are running in
Stencil's browser extension, which scans the images on the user's current web page; you
help the user find and act on those images by planning operations. You never produce
image data yourself.

Respond with EXACTLY ONE JSON object and no other text, in this shape:
{"version":1,"reply":"<short answer for the user>","actions":[...],"variants":[]}
`
)

// hasStencilPromptHead reports whether a system prompt is recognizably one of
// Stencil's own (starts with a pinned head).
func hasStencilPromptHead(s string) bool {
	return strings.HasPrefix(s, llmEditorPromptHead) || strings.HasPrefix(s, llmExtensionPromptHead)
}
