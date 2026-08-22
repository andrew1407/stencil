# LLM system-prompt asset

`systemPrompt.json` is the canonical source of the LLM system prompt's prose core
(llm-contract.md §4). Keys:

- `head` — the editor prose before the "Available ops" list (browser, desktop, cli,
  mcp, bot, pystencil — byte-identical on all six).
- `tail` — the editor prose after the ops list (same six surfaces).
- `extensionHead` — the extension's deliberately diverged head (contract §8 profile).
- `extensionTail` — the extension's diverged tail (no leading newlines; the extension
  assembles `head + '\n' + bullets + '\n\n' + tail`). The extension ships self-contained
  (MV3), so it consumes a checked-in copy of the two extension keys
  (`extension/src/config/systemPrompt.json`), drift-guarded by
  `extension/tests/dataParity.test.js`.

The assembled prompt is `head + <registry-generated op bullets> + tail` (contract §13);
`browser/js/llm/opPlan.js` does the assembly and re-exports the strings.

Byte-exactness: JSON escapes newlines but round-trips every byte — consumers get the
strings byte-identical to the old in-source literals. Do not reflow or "prettify" the
values: the collaboration server byte-pins the first 328 bytes of `head` and 434 bytes
of `extensionHead` (`server/internal/httpapi/llmprompt.go`), and
`browser/tests/systemPromptAsset.test.js` canaries both prefixes.
