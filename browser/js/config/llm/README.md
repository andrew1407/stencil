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

## Shared sentences (§4 / §7)

Every value is a plain string, so a surface embeds the file and pulls a field with no
schema of its own. These were retyped in five clients each before they moved here.

- `edgeMapSentence` — §7, appended to the system-prompt suffix when — and only when —
  the edge map actually rides along as the second attachment. Byte-identical on the six
  surfaces that attach one.
- `continuationNote…` — §7's auto-continuation note, the internal sentence a surface
  appends to the re-sent turn. **Four wordings, all of them already in production when
  the strings moved into this asset** — this file records the divergence rather than
  hiding it, and converging them is a behaviour change that belongs to its own commit:
  - `continuationNote` — the editors' (browser `chatStore.js`, desktop
    `mainWindowChat.cpp`). This is the wording llm-chat.md §12.1 quotes.
  - `continuationNoteConsole` — cli `wire.zig` + mcp `prompt.rs` (`…loaded — continue
    with it, using its real pixel size.]`).
  - `continuationNoteBot` — bot `ChatDocument.cs` (`…those actions just made — …`).
  - `continuationNotePython` — pystencil `llm.py` (`…that action just made — …`).
  - `continuationNotePrefix` — `[The working image is now`, the opening every surface's
    §12.1 machinery filter matches on. That filter is why the four wordings are
    tolerable: any bracketed variant is refused on both sides of the store.
- `contextSuffix…` — the desktop's dynamic context suffix templates
  (`contextSuffixImage`, `contextSuffixNoImage`, `contextSuffixVideoFrames`,
  `contextSuffixVideo`). `%1` / `%2` are Qt `QString::arg` placeholders, kept verbatim;
  a non-Qt consumer substitutes positionally.
- `botOpsFooter` / `consoleOpsFooter` — the §10 profile block's closing sentence, prose
  rather than a registry bullet. The bot's and the consoles' wordings differ by the word
  "console"; both are recorded.

`browser/tests/systemPromptAsset.test.js` byte-pins each of them and asserts the four
continuation wordings stay distinct, so a silent convergence fails too.
