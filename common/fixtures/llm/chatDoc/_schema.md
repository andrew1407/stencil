# chatDoc fixtures — llm-contract.md §12.1 persisted-chat document vectors

Vectors for the per-project chat document all five persisting surfaces serialize
(browser IndexedDB + server `chat` kind, desktop `projects.json`, cli/pystencil
`.stencil` `chat` key, bot server-side). Reference implementation:
`browser/js/llm/chat/store.js` (`parseChatDoc` / `buildChatDoc`,
`CHAT_DOC_VERSION = 1`, 32-message cap). Walked by
`browser/tests/llm/llmWireFixtures.test.js`.

Two files, each an array of case objects with `name` (unique) + optional `comment` and an
optional informational `divergences` map (`{ "<surface>": "one-line summary" }` of measured
surface differences on the case — never asserted; walkers ignore it, and each surface pins
its own divergent expectation in its local override file):

## roundtrip.json — fixed points
`{ name, comment?, doc }` where `doc` is a canonical §12.1 document. A walker
asserts, through ITS implementation:
1. `parse(JSON.stringify(doc))` deep-equals `doc` (and `parse(doc)` for object-input parsers);
2. re-serializing the parsed form (browser: `buildChatDoc(parsed.messages,
   parsed.savedAt)`) deep-equals `doc` — serialize∘parse is the identity here.

## tolerance.json — lenient-read pins
`{ name, comment?, doc | docString, expectParsed }`. Exactly one of `doc`
(object input) / `docString` (raw string input — malformed-JSON cases; parsers
that only take strings should `JSON.stringify` the `doc` cases). The walker
asserts `parse(input)` deep-equals `expectParsed`; `expectParsed: null` means
"treated as a missing document" (never an error). Pinned behavior:
- unknown top-level fields and unknown message fields (incl. `images`) dropped on
  read, never written back; roles outside `user|assistant` dropped; non-string
  `text` dropped;
- `version !== 1` (strict; `"1"` ≠ `1`), missing version, non-array `messages`,
  non-object doc, malformed JSON ⇒ `null`;
- `savedAt` numeric-string coerced, garbage ⇒ `0`; > 32 messages ⇒ last 32 kept;
- machinery text filtered on read: the §7 continuation note (exact or bracketed
  variant, any role) and assistant turns that are raw op-plans/ask-plans.
