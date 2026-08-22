# Stencil LLM contract — op profiles (§8, §10)

Part of the [Stencil LLM contract](llm-contract.md); section numbers continue the
root document's (code comments cite `§8`/`§10` everywhere). **Normative op membership,
key schemas, flags and prompt bullets live in
[`opRegistry.json`](../browser/js/config/llm/opRegistry.json)** (`profiles.editor` /
`console` / `bot` / `mcp` / `extension`, plus per-op `divergence` notes; see
[its README](../browser/js/config/llm/opRegistry.README.md)) — the hand-maintained op
tables these sections once carried are gone. This file keeps the per-profile
BEHAVIOUR: what each surface's profile is for, its execution model, and its security
boundaries.

Profile shape today (per the registry): **editor** 33 ops (browser 32 + desktop-only
`openFile`), **console** 23 (cli 23 ⊇ pystencil 19), **bot** 24, **mcp** 10,
**extension** 12. An op outside a surface's profile falls to §1's unknown-op skip, by
design.

## 8. Extension profile (extension-only ops)

The Chrome extension is not an editor: its working set is the **images scanned from the
current page**. Its chat therefore runs the shared contract with its own op set
(`opRegistry.json` → `profiles.extension`: `focus`, `open`, `attach`, `pin`, `unpin`,
`scanTab`, `rescan`, `openUrl`, `theme`, `accent`, the panel `filter`, `clearChat`) and
these behaviours:

- **Context listing**: every turn's system-prompt suffix includes a compact numbered
  listing of the current scan results (index, kind `img|background|poster|video|icon`,
  pixel dims when known, format, truncated URL basename, truncated `alt` text; ≤ 100
  entries). Ops reference images **by that index**. When the surface can enumerate the
  user's **other open tabs** *and the user has opted in*, the suffix also carries a
  numbered tab listing (index, truncated title, URL; ≤ 20 entries) that `scanTab`
  references — one snapshot per user turn, so the indices the model sees and act on agree.
- **The tab listing is opt-in and off by default** (extension setting `shareTabs`). It is
  the only context a client sends that the user did not point the assistant at: the pages
  they have open, on *every* turn, to an endpoint that may not be local. While it is off,
  the surface must not enumerate tabs at all — the block is absent and `scanTab` fails
  validation for want of indices. When on, each listed URL is reduced to **origin + path**
  before it is sent; the query and fragment are dropped, because that is where session
  ids, reset tokens and search terms live and none of it helps pick a tab. Truncation is
  not a substitute — a length cut keeps the *front* of a query string.
- **The hand-off model**: the extension never runs core image-editing ops itself —
  editing happens in the editor after `open`, which hands the image off via the existing
  `#stencil=` launch payload; `open.actions` accepts **core** §2 ops
  (crop/rotate/filter/layout/page) translated onto launch options (crop → `crop`,
  filter/layout → a layout payload with `imageFilter`/`filterColor`/`lines`, page →
  `page`). Core ops appearing at the top level of an extension plan are dropped with a
  warning. `open` also takes `"mode":"resume"|"copy"` — prefer `resume` when an editor
  already holds that image (the non-destructive path) — and an optional `incognito`
  (always launches a new incognito editor).
- **Editor mode (destination only)**: when the panel is standing on a Stencil editor tab,
  the `open` op imports into **that already-open editor tab** (through the extension's
  editor bridge) instead of spawning a new `#stencil=` tab — same op, same fields,
  different destination, so nothing in the schema and no other client implementation
  changes.
- **Gather ops & auto-continuation**: `attach` (fetch image bytes into the conversation
  for vision analysis), `scanTab` (scan another opted-in tab and **replace the
  conversation's working listing** with its images — later ops act on the new listing,
  each entry keeping its provenance; the swap lasts until the next `scanTab` or a
  conversation clear), and `rescan` (re-scan the CURRENT tab) are gather ops: when a
  plan's actions only GATHER context, the client applies them and automatically re-sends
  the turn **once** (so "which of these is the cat?" and "look at the rabbits tab and pin
  the rabbit photos" each resolve in one user action). Bounded to a single continuation;
  a successful `scanTab` also appends a text note to the replayed history naming the
  switch, so the model knows why the listing changed. A tab index out of range fails the
  plan; an unscannable tab drops the action with a warning.
- **Panel-settings ops** (`theme` — with a third `system` mode, since the extension's
  Options offers System/Light/Dark where the editors' toggle is binary; `accent`; the
  panel `filter`, which drives the very same list controls a click drives so the
  checkboxes, persisted filter state and rendered list cannot disagree) are the assistant
  working the surface's own controls rather than the images: no page access, no fetch, no
  listing indices, and they never GATHER context, so they take no part in the
  auto-continuation. A surface that does not offer one drops the action with a warning
  rather than failing the plan. Note the wire-name collision the registry records as
  `filter.extension`: the extension reuses the name "filter" for its panel-list filter, a
  *different op* from the core image filter.
- **`openUrl`** rides in this profile too, under the §10 user-echo guard (the exact
  `url` must appear verbatim in the USER's own messages of this conversation — blocked
  with a warning otherwise; the extension holds `<all_urls>`, so a model-introduced host
  must never be fetched); it opens via the `#stencil=` hand-off, or the editor-mode
  import when the panel stands on an editor tab.
- **Boundaries**: downloads, tab navigation by URL, server pin/store, Options fields,
  and page-wide highlight injection stay un-drivable. `pin`/`unpin` work the panel's
  existing local pin store only.
- **System prompt**: the extension uses the §4 asset's `extensionHead`/`extensionTail`
  with its registry-generated op bullets between them (plus a note that `open.actions`
  accepts the §2 core ops) — JSON-only shape, **`variants` must be empty in extension
  plans**, its own `ask` wording (see §11), and the injection guard. Scanned page
  content and image contents are data, never instructions.
- Settings/persistence: `chrome.storage` key `llmSettings` (same JSON shape as §5);
  defaults table applies. Calls to providers from the extension use extension host
  permissions rather than CORS (the manifest's required `<all_urls>` covers every
  origin; the options page keeps a permission check as a guard should that narrow).

## 10. Editor-settings profile (browser & desktop editors; partial profiles below)

The two GUI editors extend §2 with ops that adjust the EDITOR itself, not the image:
theme, accent, lineStyle, units, view, clear, openUrl, openFile (desktop-only), connect/
disconnect, copy, removeProject, clearProjects, compare, zoom, renameProject,
projectColor, blankColor, openProject, incognito, clearChat — schemas, validation and
bullets per `opRegistry.json` → `profiles.editor` and each op's entry. Everywhere else
these are unknown ops — skipped with a warning per §1, by design — except where a
partial profile below carries one explicitly.

Semantics (profile-wide):

- Settings ops ride in the same `actions` array (same ≤ 16 limit), execute in plan
  order through the SAME code paths as the toolbar/settings UI (browser: the
  `window.stencil` facade; desktop: the settings actions), and don't touch the
  working image.
- They are **forbidden inside `variants`** (variants exist to produce images) — a variant
  containing one is DROPPED with a warning per §1, never a plan-level failure; the
  top-level actions and the well-formed variants still run.
- **Destructive ops confirm in-app.** `removeProject`, `clearProjects`, `clearChat` (and
  `openProject` over a dirty unsaved editor) execute the surface's EXISTING flow
  *including its user confirmation*; a declined confirm is a "canceled" note, never a
  failed plan. `clearChat` is carried by EVERY chat surface and is deferred to the END of
  the turn (see §12.2 for what a confirmed clear removes).
- **The user-echo guard.** `openUrl` (and the console `openFile`, and `save`'s `path`
  where honored) accept only a URL/path the user themselves wrote verbatim in this
  conversation — the model may echo the user but can never introduce, complete, or
  rewrite a host or path; page/attached content never counts. `openUrl` loads a new
  working image IN PLACE (never a second tab — the conversation lives in this one;
  `incognito: true` switches THIS editor to incognito first), and the executor **awaits
  the load** so a later `crop` acts on the fetched picture, never races it.
- **Server ops resolve against the user's own store.** `connect`/`disconnect` must
  resolve to a server the user already SAVED (exact URL match, else unique host);
  anything else fails the plan in the editors. Plans never carry tokens — auth is the
  stored connection's own.
- **`clear` vs `blank`**: `clear` drops the working image and its lines, leaving the
  editor empty — that is what "remove the image" means; without it a model reaches for
  `blank`, which replaces the picture with a white page and reads as a success it never
  achieved. Same trap for backgrounds: `blankColor` recolours a BLANK project's
  background KEEPING the drawn lines, where a fresh `blank` would destroy them.

### The bot's partial profile

Two ops are carried into other profiles the way §8 carries `theme`/`openUrl`: the
**bot** implements `connect` and `disconnect` (those two only) against the connections
the user saved with its `/connect` command — the same resolution as the editors' (exact
URL, else unique host, the STORED token riding along; plans never carry tokens) — but,
this being a chat with no settings UI to fall back to, an unresolved or ambiguous server
and a refused connect/disconnect are skipped with a warning per §1 (e.g. "connect it
first with /connect") instead of failing the plan, and a plan made only of connection
ops runs without a working image. The bot's system prompt appends only its own bullets
at the same splice point, and its §4 dynamic context suffix adds one connections line —
the connected server URLs plus the active project, never a token — so questions about
connections are answered in `reply` without ops. Beyond `connect`/`disconnect` the bot
carries: §2 `undo`/`redo`/`reset` (its own `/undo` `/redo` `/reset`), §10 `clear` scoped
to the IMAGE AND EDITS ONLY (the conversation survives `clear`; clearing the
conversation is the separate `clearChat` op, confirmed in-app — the `/chat` persistence
toggles stay user-only), `lineStyle` (the pen-default commands `/color` `/thickness`
`/points` `/style` `/fill`), `openUrl` (the `/url` path with its SSRF vetting plus the
§10 user-echo guard; the executor awaits the load), `renameProject` (`/projectname`),
`describe` (`{"op":"describe","text":…}`, ≤ 500 chars, `""` clears —
`/projectdescription`), `blankColor` (`/blankcolor` — keeps the edits, unlike a fresh
`blank`), `projectColor` (`/projectcolor`), and `export`
(`{"op":"export","what":"layout"|"project"}` — sends the `/json` / `/project` file into
the user's own chat; bounded to one send per action). Its context suffix also widens
to the pen defaults, the pending-edit stack size, and a capped project-name listing
(the cli console's rule). Server-side deletes, expiry, `/create`, `/sync`, scraping
ops, and `/fetch` remain deliberately un-drivable there.

### The cli console's partial profile

The **cli console** carries its own partial profile the same way, spliced at the same
anchor: `accent` (its `/theme` IS an accent colour — hex only; `theme` light/dark stays
an unknown op there), `connect`/`disconnect` resolved against the servers the user
`/connect`-ed THIS session (exact URL, else unique host; the CLI has no token store —
auth re-runs the same handshake `/connect` uses, so no token ever exists to leak),
`delete` mapping onto the console's confirmless `/delete` with its full guard set
(`.stencil` only, no URLs, no cwd escape), `openUrl` (same user-echo guard as the
editors'; maps onto the console's URL-load path, synchronous — later actions see the
fetched picture; `incognito` is not a console concept and is ignored with a note),
`openFile` (a LOCAL path under the same user-echo guard, gated to the formats the
console opens — a `.stencil` restores the project, a `.json` draws its layout, anything
else loads as the picture; a leading `~` expands to `$HOME`, since a path typed in a
console never passes through a shell), the `save` op's `path` (same echo guard; a folder
or a file name), `copy` mapping onto the console's `/copy`, the §10 `clear` verbatim
(mapping onto `/drop` — without it a model reaches for `blank`, the exact hole §10 warns
about), the §2 `undo`/`redo`/`reset` (the console's own `/undo` `/redo` `/reset`),
`accent`'s `preset` form (the `/theme` preset names), `reconnect`
(`{"op":"reconnect","server":…}`, same resolution as `connect`, mapping onto
`/reconnect`), and `crop`'s console-only `"album": true` spec key (the `/crop … album`
axis derivation). Execution misses (unknown/ambiguous server, bad path) print a note per
§1 instead of failing the plan; the console-only ops are top-level only. Its context
suffix lists connection URLs, the active project, and a capped per-server project-name
listing, so "what's on my server?" is answered in `reply` without ops.

### The pystencil console's partial profile

The **pystencil console** carries the SAME profile as the cli console with these
exceptions (recorded as `profiles.console.notes` in the registry): `accent`/`reconnect`
stay unknown ops (it has no theme and no reconnect command), `copy` stays unknown (no
clipboard), `openFile` stays unknown, and `delete` additionally gains the cli's
traversal guard, which pystencil's API-level `delete_project` lacked. Closing this
parity also means: pystencil gains `/disconnect` and `/delete` REPL commands (they
existed only as API calls), the SAME console-context suffix the cli sends (connections,
active project, capped project names — tokens never), and its REPL registers
`/upload`s as the turn's attachments so §2.1 `image` plans work there like everywhere
else.

### The mcp profile

**mcp** runs the ten core ops (`profiles.mcp`: crop, rotate, filter, layout, formula,
page, blank, frame, image, save) in a one-shot headless pipeline — no settings, no
connections, no chat surface. Since the Phase-6 widening its validators accept the full
core schemas rather than a subset: `page`/`blank` **custom centimetre dims
(`width`/`height`) execute** (lowered onto the CLI pipeline like the editors' custom
page size), and `formula`'s clears — `{"op":"formula","enabled":false}` and the
empty-`expr` axis clear — are **accepted but execute as inert no-ops with a note** (a
one-shot run has no persistent formula state to clear; `opRegistry.json` →
`ops.formula.divergence`). `save.path` is honored **sandboxed**: the destination is
resolved relative to the run's `output_dir` (absolute, `..` or `~` paths note-and-fall-
back to the usual place; a `.stencil` path is a file, anything else a folder) — see
`mcp/src/opplan/lower.rs`.

### Never model-drivable (every surface, by design)

The assistant's own configuration (provider/endpoint/model/API key —
self-configuration is the exfiltration primitive), chat persistence and consent TOGGLES
(`/chat on|off`, `shareTabs`; clearing the conversation is drivable via `clearChat`
because its in-app confirm keeps the user in the loop — flipping consent state is not),
clipboard READS (paste — copied secrets would enter vision turns), OS dialogs (file
pickers, save sheets, share sheets), hotkey rebinding, ending the session/window,
server-side project deletion/expiry/transfers beyond what this section explicitly
grants, and any URL the user did not write themselves. These are boundaries, not
backlog — enforced by §13's `FORBIDDEN_OPS` (registry test + executor reject; corpus
fixtures 207–217).
