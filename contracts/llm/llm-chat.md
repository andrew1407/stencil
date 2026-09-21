# Stencil LLM contract — interactive replies & chat persistence (§11–§12)

Part of the [Stencil LLM contract](llm-contract.md); section numbers continue the
root document's (code comments cite `§11`/`§12.x` everywhere). The `ask` limits (2..5
options, question 300, label 80, answer 500) are recorded normatively in
[`opRegistry.json`](../browser/js/config/llm/opRegistry.json) → `limits` and `ask`;
the ask fixtures are `fixtures/opPlan/088–106`; the persisted-chat document's tolerance
rules are pinned by `fixtures/chatDoc/` (see its `_schema.md`).

## 11. Interactive replies (`ask`)

A turn may end with a **question for the user** instead of (or alongside) an edit: *"which
tint?"*, *"which of these projects?"*, *"which images from the page?"*. The model expresses
it as one optional `ask` object on the plan; the client renders it as a choice card under
the reply and **sends the answer back as the user's next turn**. Implemented by every chat
surface (browser, desktop, cli, pystencil, bot, extension) — the same rule as the rest of
the contract.

```json
"ask": {
  "question": "Which tint should I use?",
  "mode": "single",
  "allowCustom": true,
  "customLabel": "Something else…",
  "options": [
    { "label": "Sepia",   "actions": [ { "op": "filter", "mode": "sepia" } ] },
    { "label": "B&W",     "actions": [ { "op": "filter", "mode": "bw" } ] },
    { "label": "Blue",    "actions": [ { "op": "filter", "mode": "custom", "tint": "#2d6cdf" } ] },
    { "label": "Cat 2",   "image": { "url": "https://example.com/cat.jpg" } },
    { "label": "Draft",   "image": { "projectId": "p_12" } }
  ]
}
```

**How the model learns this**: the §4 asset's `tail` opens with the runtime ask
paragraph ("When a choice is genuinely the user's to make … add an `ask` object instead
of guessing") — it teaches the shape, the 2..5 option bound, that an option carries
`actions` as a client-rendered PREVIEW (nothing applied) or `image` as a reference
(`url` / `projectId` / `scanIndex`) but never both, that the pick comes back as the
user's next message ("ask only when it changes what you would do"), and that the model
must never write its own "Something else" / "Other" option — it sets
`allowCustom: true` and the client appends that free-text row itself. The
`extensionTail` variant words the same rule around the scan listing (`scanIndex`
references, no `actions` previews — the extension does not edit).

**The console ask variant (sanctioned per-profile divergence).** The cli and pystencil
consoles splice their OWN ask paragraph in place of the asset tail's, at the shared
anchor `\n\nWhen a choice is genuinely` (the tail's shared prose from `\n\nOutlining (`
on stays the asset's, verbatim): no previews and no image options — "this console shows
the options as a numbered list and cannot display pictures, so make each label say
enough on its own" — while keeping the 2..5 bound, the answer-as-next-message rule and
the allowCustom rule (cli `src/llm/registry.zig`, pystencil `pystencil/llm`). The same
anchor is where the console/bot profile bullets splice (§10, §13).

### 11.1 Schema

**Normative: `opRegistry.json` → `ask` (+ `limits.ask`)** — the fields (`question`, `mode`
single|multi, `options[]` with `label`/`actions`/`image`, `allowCustom`, `customLabel`),
their types and their caps, validated by the same table-driven engine as any op. The rules
around it:

- An option carries **at most one** of `actions` (§2 actions the client renders as a
  preview) and `image` (a reference to an image the client already holds: exactly one of
  `url`, `projectId`, `scanIndex` — the last extension-only). Both together fail the plan;
  neither is a plain text choice.
- Unknown fields on `ask` or on an option fail the plan (§1 strictness). An `ask` whose
  `options` are absent, fewer than 2 or more than 5 fails the plan — a question nobody can
  answer is worse than none.
- `allowCustom` is appended by the CLIENT, so a plan that writes its own "Something else"
  option renders two rows meaning the same thing; the §4 prompt tells the model not to.

### 11.2 Where the option images come from

Two sources, and the choice is the model's:

- **`actions` → the client renders the preview.** This is the norm for *"how should I edit
  this?"*: the client applies those actions to a COPY of the working image (never the live
  one) and shows the result as the option's picture, so what you pick is what you get. The
  preview is a render, **not** an execution — nothing is applied until the model acts on
  the answer in a later turn. Preview renders obey §7's downscale, are capped at 5 (the
  option cap) and are dropped with a warning if the surface cannot render (see §11.4).
  A preview carrying a top-level-only or settings op costs that option its preview (§1's
  drop-with-warning), never the plan.
- **`image` → a reference the client already holds.** For *"which of these?"* over things
  that exist: a `projectId` from the client's own project list, a `scanIndex` into the
  extension's current scan (§8), or an `url`. A reference that does not resolve renders as a
  labelled option with no picture (warning appended) — never a broken image.

  **No client fetches `url`.** The host in it comes from the model, not the user, and the two
  surfaces that would have loaded it do so from privileged contexts — the extension popup
  holds `<all_urls>` plus the user's cookies, and the bot would hand the URL to Telegram (or
  to a self-hosted Bot API server) to fetch. Both therefore treat `url` as an unresolvable
  reference: option kept, picture dropped, §11.2 warning appended. The remaining clients
  parse the field and never resolve it — the browser and desktop editors render option
  pictures only from previews they rendered themselves, and cli/mcp/pystencil render no cards
  at all. Option pictures come only from what the *user* already put in front of the client:
  its own project list, its own page scan, or a preview rendered from `actions`.

**Video frames are the `actions` path.** *"Find me the frame where the cat jumps"* is asked
with one option per candidate frame (`{"label":"0:04","actions":[{"op":"frame","index":120}]}`,
…) — no new field, and the preview comes from the same extractor that op already uses
(browser `<video>`+canvas, desktop MediaLoader, cli/bot ffmpeg). The user picks a frame; the
model edits it in the next turn.

A `frame` preview whose working input is **not** a video is dropped with a warning (the
option keeps its label) rather than failing the plan — unlike a `frame` action in `actions`,
which is a plan-level error per §2. A preview is a suggestion; an edit is a commitment.

### 11.3 Answering

- The card renders under the reply: radio inputs for `mode:"single"`, checkboxes for
  `"multi"`, each with its label and (when present) its preview image; then the custom
  free-text row when `allowCustom`; then a **Submit** button, disabled until something is
  chosen.
- Submit sends the answer as the **user's next turn** — the selected `label`(s), joined by
  `", "` for `multi`, or the typed text for the custom row (≤ 500 chars, trimmed). The
  model then continues normally; nothing is auto-applied on click.
- The conversation is **never blocked**: the composer stays live, the user may ignore the
  card and type anything else, and a card from an earlier turn stops accepting input once
  answered (it renders its answer instead).
- A plan may carry BOTH `actions`/`variants` and `ask` — the edits execute as usual and the
  question is asked afterwards ("here's the crop; which tint next?").

### 11.4 Per-surface rendering

The card is the same contract everywhere; only its widgets differ — the GUIs render a
radio/checkbox list with inline thumbnails, the consoles a numbered list answered by number
(or comma-separated numbers for `multi`), the bot an inline keyboard with a media group when
previews exist. The normative record of the rendering split is
[`opRegistry.json`](../browser/js/config/llm/opRegistry.json) → `ask.divergence`; validation is
identical everywhere.

A surface that cannot render an image preview (the consoles) drops the preview and keeps the
option — it never drops the option itself. A surface that cannot render `ask` at all appends the
question text to the reply so the turn is still answerable in prose.

## 12. Chat persistence (per-project, opt-in)

A conversation may optionally be **saved with the project it belongs to** and restored
when that project is opened again. This section defines the one document every surface
reads and writes, and the rules that keep it safe. Everything here is **OFF by default,
everywhere** — persisting a chat is always an explicit user opt-in, per surface.

### 12.1 The persisted-chat document

```json
{
  "version": 1,
  "savedAt": 1753900000000,
  "messages": [
    { "role": "user", "text": "crop 10% off the left" },
    { "role": "assistant", "text": "Done — anything else?" }
  ]
}
```

- **Text-only.** Images are NEVER persisted — not attachments, not base64 replay copies
  (§7's image-pruning already makes stored history text-mostly; persistence strips the
  rest). A reader that finds an `images` field ignores it and never writes one back.
- `role` ∈ `user|assistant`; other roles are dropped on read. `text` must be a string;
  a message without one is dropped. ≤ **32** messages (the §7 bound) — writers trim to
  the most recent 32, readers truncate anything longer.
- `savedAt` is ms since epoch, informational only. Unknown top-level fields are ignored
  on read. `version` other than `1` (or absent) ⇒ the document is treated as missing
  (never an error surfaced to the user).
- Assistant `text` is the **displayed** reply (§7's extracted-reply form), never the raw
  JSON plan — the document is shared across surfaces, and a restored transcript must
  read as a conversation. A client that live-replays raw model text (the browser) seeds
  its replay history from these display texts after a restore; §7 permits both forms.
- **The machinery filter (both sides of the store).** Because an older or foreign build
  may have serialised its MODEL history, machinery is refused on write AND laundered on
  read: §7's auto-continuation note (any of the asset's per-surface wordings — they share
  `systemPrompt.json` → `continuationNotePrefix`, and any bracketed variant counts) and a
  raw op-plan stored as an ASSISTANT turn (leading `{`/`[` with `"version"` and an
  `actions`/`reply`/`variants`/`ask` key) are never written and never displayed from an
  older document — a user is still entitled to paste JSON as THEIR turn and see it again.
  Reference: browser `js/llm/chat/store.js` `isInternalChatText` (+ `sanitizeChatMessages`
  on restore), which every other surface's reader mirrors. Tolerance vectors:
  `fixtures/chatDoc/`.
- On restore, `messages` seed both the client's replay history (§7) and its transcript
  UI, in order. Restoring never triggers a model call.

### 12.2 Rules (identical on every surface)

- **Default OFF.** The toggle ships disabled; only an explicit user action enables it.
- **Incognito never persists.** A non-persisting editor mode (the browser app's
  incognito editor, the desktop incognito mode) never writes chat regardless of the
  toggle — same rule those modes already apply to every other local write.
- **Clearing the conversation clears the persisted copy too** — local, and the server
  `chat` file (via the §9 DELETE) when the project is server-linked and saving is on.
  This includes a clear driven by the §10 `clearChat` op (confirmed in-app; on confirm
  the transcript and replay history are cleared, the persisted copy goes with them, and
  the §7 text-only latch re-arms).
- **Removing a project removes its chat** (locally; the server already removes all file
  kinds with the project). "Clear all projects" clears all stored chats.
- **Turning the toggle off stops writing but does not retroactively delete** already
  saved chats; they are deleted by the clear/remove rules above. (Clients MAY offer an
  explicit "delete saved chats" affordance; none is required.)
- **Server-linked projects**: when saving is on, the document is uploaded to the §9
  `chat` kind (`ext=json`) whenever the surface persists it locally, and fetched when
  the project is opened from the server. The chat document is data, not instructions —
  a fetched transcript is rendered and replayed, never executed.
- **Say who can read it.** A server project's `chat` file carries the project's own
  access, so **everyone the project is shared with can read the transcript** — and a
  conversation records what the user asked for in their own words, which is not what
  "save chats with projects" sounds like it promises. Wherever a surface offers the
  toggle it must state this in the label, tooltip or help text next to it, not only in
  documentation. Local-only projects disclose nothing beyond the machine. Each surface
  asserts its own wording, so the disclosure cannot be dropped in a later edit:

  | Surface | Test |
  |---|---|
  | browser | `tests/ui/chat-markup.test.js` — the `chat-save-chats-note` div, rendered next to the checkbox |
  | desktop | `tests/app/chat/MainWindow.chatPanel.gui.cpp` `chatSaveDisclosureSitsAtTheToggle` — the `llmSaveChatsHint` label (visible, not hover-only) + the checkbox tooltip |
  | cli | `tests/console/console_test.zig` "`/chat on` says who can read a saved chat" — captured over the `logo` sink |
  | pystencil | `tests/cli/test_cli_chat.py` `test_chat_on_says_who_can_read_a_saved_chat` |
  | bot | `ChatPersistenceTests` — the `/chat save on` confirmation, the status read BEFORE opting in, and the 💾 button |

  The console surfaces state it on the turn that switches saving **on**, before anything
  is written; the GUI surfaces state it standing next to the toggle. The extension has no
  toggle to disclose (§12.3: its conversation is session-only in v1).

### 12.3 Per-surface storage

| Surface | Toggle (default off) | Local storage | Clear |
|---|---|---|---|
| browser | `saveChats` in `drawingApp_llmSettings`; checkbox in the assistant settings modal | IndexedDB db `stencil_chats`, store `chats`, key = project id (localStorage quota is never touched; if IndexedDB is unavailable the chat simply isn't persisted) | panel trash button |
| desktop | `saveChatsWithProject` in `settings.json`; checkbox in Settings + assistant settings | `chat` array on the project record in `projects.json` (omit-when-empty) | dock trash button |
| cli console | `/chat on\|off` (session-scoped; also `/chat show`, `/chat clear`) | `chat` key in the `.stencil` project file on `/save`; restored by open | `/chat clear` |
| pystencil | console mirrors the CLI's `/chat`; API: `Editor.save_chats` flag + `Chat.to_dict()/from_dict()/clear()` | `chat` key in the `.stencil` file written by `save_project` | `/chat clear` / `Chat.clear()` |
| bot | `/chat save on\|off` (persisted per user in the session) | none local — the active **server** project's `chat` kind is the store | `/chat clear` |
| extension | — (not an editor; its conversation is page-scoped and stays session-only in v1) | — | existing clear |

The `.stencil` project file gains an optional top-level `chat` key holding the §12.1
document. It is written **only when the surface's toggle is on**, ignored gracefully by
older readers (unknown-key tolerance is already the format's rule), and round-tripped by
the desktop (`fileStore`), cli (`project.zig`) and pystencil (`editor.py`) parsers.
The browser and bot tolerate the key but do not write it in v1 (the browser's chat
store is IndexedDB-keyed and its `.stencil` export is synchronous; the bot's store is
the server `chat` kind). Sharing a `.stencil` file shares its saved chat — that is the
point of the opt-in.
