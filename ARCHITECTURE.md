# Stencil architecture

The design rulebook. Read this before extending anything; `CLAUDE.md` is the per-task
cheat sheet, this is the *why* and the *where*. Enforcement lives in `.claude/rules/`.

One shared C++ core feeds four front-ends; four more subprojects are protocol adapters over
the CLI or the server. Everything below follows from that shape.

---

## 1. The core parity contract

`core/` (C++17, **STL-only, codec-free, GUI-free**) is the shared logic. Three surfaces run
it, by three different mechanisms:

| Surface | How it gets `core/` | ABI file |
|---|---|---|
| browser | compiled to wasm (`npm run build-wasm`), **plus a JS fallback that must match** | `core/wasmApi.cpp` + `wasmCropApi`/`wasmStateApi`/`wasmProjectsApi.cpp` |
| desktop | `add_subdirectory(../core)` — links the CMake library | (direct C++) |
| cli | `cli/build.zig` **recompiles the sources** | `core/cliApi.cpp` |
| pystencil | `pystencil/build.py` **recompiles the sources**, driven by ctypes | `core/cliApi.cpp` |

Four rules, all of them load-bearing:

1. **Each core module is a port of a named `browser/js/` call site.** The mapping is at the
   top of every core header. Behaviour must stay identical down to edge cases; `core/tests/`
   are ports of `browser/tests/`. Change one side, change the other, update both suites.
2. **The JS fallback must match wasm op-for-op.** `browser/tests/wasm-parity.test.js` proves
   it and CI builds wasm fresh to run it. This is what catches divergence.
3. **No `eval`, either side.** `browser/js/core/formulaEngine.js` and `core/parse/formulaParser`
   are both real recursive-descent parsers: `+ - * / ** ( )`, one variable, `**`
   right-associative, empty = identity, div-by-zero/overflow = invalid, recursion capped at
   the same depth (`MAX_DEPTH` ↔ `kMaxDepth`).
4. **The source list lives in three files.** Adding/removing/renaming a `core/*.cpp` means
   editing `STENCIL_CORE_SOURCES` in `core/CMakeLists.txt`, the array in `cli/build.zig`, and
   the list in `pystencil/build.py`. A new wasm export also needs `_stencil_x` in
   `EXPORTED_FUNCTIONS` (`core/CMakeLists.txt`) — a separate list.

Codecs, HTTP, JSON, video, QImage/canvas rendering, persistence and the event loop are the
**adapters'** job. Never in `core/`.

`mcp/`, `server/`, `bot/` and `e2e/` never link or recompile `core/` — the parity contract
does not reach them. Their contract is the CLI's argv/stderr shape and the server's wire
protocol.

---

## 2. Shared-data rails

**`browser/js/config/` is the canonical home for every shared data table.** Nothing else is a
source of truth. Today that covers accents, colour names, icons + icon motion, page/app
constants, hotkeys, help text, layout fields, media types, theme tokens, and the three LLM
assets under `llm/` (`opRegistry.json`, `systemPrompt.json`, `providers.json`).

Five consumption mechanisms — pick the one your surface already uses, never invent a sixth:

| Mechanism | Surface | Shape |
|---|---|---|
| **qrc alias** | desktop | `<file alias="X.json">../../browser/js/config/X.json</file>` in `desktop/resources/app.qrc` |
| **`@embedFile`** | cli | `mod.addAnonymousImport("X.json", …)` in `cli/build.zig`, then `@embedFile("X.json")` |
| **`include_str!`** | mcp | `include_str!("../../browser/js/config/X.json")` |
| **`<EmbeddedResource Link>`** | bot | `<EmbeddedResource Include="../../../browser/js/config/X.json" Link="Assets/X.json" />` |
| **checked-in copy + byte-equality drift test** | extension, pystencil | the copy ships with the surface; a test pins it to the canonical file |

The fifth rail exists only where embedding is impossible: the extension ships self-contained
(MV3 reads nothing outside its own tree) and pystencil must stay relocatable. Its copies are
guarded by `extension/tests/dataParity.test.js` (modes `full` / `subset`, with declared
`extensionOnly` names) and `pystencil/tests/test_canonical_drift.py`. **A copy without a
drift test is a bug.**

Where a value can be *computed*, prefer computing it: page formats and colour names are
read back out of the core over the C ABI rather than mirrored (`pystencil` does this;
desktop and cli drift-test `PAGE_SIZES` against the core).

The **op registry table-drives all seven op-plan validators.** Each surface keeps only its
normalizers, its executors and the few native rules the registry names. Change a key's type,
range, enum or cap in `opRegistry.json` and every surface changes with it —
`browser/js/config/llm/opRegistry.README.md` is the spec. After any registry edit run
`cd browser && npm run gen-fixtures` (the browser walker fails while the generated bundle
is stale).

---

## 3. Layer model, per app

Imports point **downward only**. A layer may use everything to its left and nothing to its
right. A "layer" is a role, not necessarily a directory that exists yet. Most of this is now real
and lint-enforced (`desktop/tests/layerBoundary.headless.cpp`, cli's layer lint, the import
direction checks); where a ring is still a target, it says so.

**browser** — `config/` + `utils.js` → `core/` (pure logic, **no DOM**) → bus (`core/emitter.js`)
→ `net/` → `llm/` → console facade (`console/stencilApi.js`) → `ui/` (pure string-returning
components) → render.
`core/` must stay DOM-free so wasm parity is testable. `ui/` never reaches into `net/` or
`llm/`; it emits on the bus. Every mutation — toolbar, hotkey, console script, LLM plan —
routes through the same core methods via the frozen `window.stencil` facade.

**extension** — `lib/` → `config/` → `llm/` → `background/` → `content/` → `popup/`,
`options/`, `crop/`.
`lib/` is the shared, dependency-free bottom; several of its modules are byte-for-byte ports
of `browser/js/ui/` files and must be synced when the browser side changes. The extension
never reads `../browser` at runtime.

**desktop** — model → controllers → `net/`, `io/` → `support/` (motion, theme, widgets and
platform helpers, currently one flat directory) → `canvas/`, `dialogs/`, `llm/` → `app/`.
The model ring is the one part of this that is still a *target*: a `CoreFacade` +
`DocumentModel` pair was attempted and deliberately backed out, because the signatures in
`mainWindow.hpp` speak `core::PageSize`/`Point`/`UnitFormat`/`ProjectMeta`/`ProjectsStore`, so a
facade that actually removes those includes is a type-vocabulary change across ~40 call sites.
`tests/layerBoundary.headless.cpp` enforces the rest, and its allowance list is the worklist
for that change whenever someone takes it on.
`app/` is composition and the Qt main window; it owns no logic that a controller could hold.
QSS belongs to the shared ID-selector sheet in `support/theme.cpp`, not to a widget's own
`setStyleSheet` (a local sheet silently changes child metrics).

**cli** — core wrap (`core.zig`) → params (`args.zig` + `params/`) → `net.zig` → ops
(`pipeline/`, `image.zig`, `layout.zig`, `page.zig`, `video.zig`) → `llm/` (`llm.zig` is a
façade that re-exports it) → presentation (`console/`) → console app (`main.zig`).
**`console/` is the only layer allowed to write to a terminal.** Lower layers return values
and errors; they do not print. (Several older modules still print directly — moving those up
is part of the refactor, not a licence to add more.)

**server** — `cmd/` → `internal/httpapi` (**transport only**: decode, authorize, encode) →
service → `internal/store` + `internal/filestore` → `internal/hub` → `internal/protocol`.
No business rule lives in a handler. `internal/protocol` is the wire contract the four
front-ends mirror; `internal/ratelimit`, `internal/auth`, `internal/config` are shared
infrastructure. The server never touches `core/`.

**bot** — four rings under `bot/src/Stencil.TelegramBot.<Ring>/` (paths below are named
by ring), dependencies pointing **inward**: `Domain` (no dependencies) ←
`Application` (use cases) ← `Infrastructure` (CLI spawn, HTTP, Telegram, filesystem) ←
`Bot` (composition root + the Telegram edge). `Domain` must stay free of Telegram, HTTP and
process types.

**mcp** — `server/` + tools → `opplan/` → `args/` → `pipeline/` → `llm/` (all three were
single `.rs` files before the split; `llmtransport/` sits beside `llm/`).
A thin adapter: its whole contract is the CLI's documented flags and its
`wrote {path} ({w}x{h})` / `error:` stderr output.

**pystencil** — `_native.py` + `core.py` → `image.py`, `codecs/`, `layout.py` → `editor/` →
`llm/`, `server/`, `sitesource/` → `cli/`. Stdlib only, ctypes only; `_net.py` is the single
fetch guard every network path goes through. (`codecs`, `editor`, `llm`, `server` and `cli` are
packages now, not modules.)

---

## 4. Size and comment budgets

Every surface carries a **ratchet**: a JSON budget plus a test that reads it.

| Surface | Budget | Test |
|---|---|---|
| browser | `browser/tests/sizeBudget.json` | `browser/tests/sizeBudget.test.js` |
| extension | `extension/tests/sizeBudget.json` | `extension/tests/sizeBudget.test.js` |
| core | `core/tests/sizeBudget.json` | `core/tests/sizeBudget.test.cpp` |
| desktop | `desktop/tests/sizeBudget.json` | `desktop/tests/sizeBudget.headless.cpp` |
| cli | `cli/tests/size_budget.json` | `cli/tests/size_budget_test.zig` |
| mcp | `mcp/tests/size_budget.json` | `mcp/tests/size_budget_test.rs` |
| pystencil | `pystencil/tests/size_budget.json` | `pystencil/tests/test_size_budget.py` |
| server | `server/internal/lint/sizebudget.json` | `server/internal/lint/sizebudget_test.go` |
| bot | `bot/tests/…/SizeBudget.json` | `bot/tests/…/SizeBudgetTests.cs` |

The ratchet enforces three things:

- **`maxNewFileLines` is 230** on every surface. A *new* file over 230 lines fails.
- **A listed file may not grow.** Shrink it and lower the recorded number in the same commit.
- **A directory's comment share may not rise.** Comments are capped as a *ratio*, so padding
  a file with prose costs you elsewhere.

Never raise a number without a note in the budget's `exceptions`. Lowering numbers as code
moves out is the point of the mechanism.

### Where the tree stands (2026-09-12)

Files over 230 lines, after the decomposition rounds. Clean means nothing over the cap.

| Surface | Over 230 | Notes |
|---|---|---|
| pystencil · bot · server | **0** | clean |
| core | 1 | `raster/rasterize.cpp` 247 — in all three build lists; its seam is file-local statics on the byte-exact hot path |
| mcp | 1 | `prompt/response.rs` — 91 production lines; mcp's ratchet measures production, not inline tests |
| browser/css | **0** | clean — `layout.css` 1211 split into `css/layout/` (14 sheets, 25–182 lines) |
| e2e/tests | 4 | `chat.spec.js` 643 and three smoke specs |
| cli/src | 19 raw | **0 real** — cli counts pre-`test {}` lines; `validate.zig` is 779 raw / 189 production |
| extension/src | 11 | 6 are byte-pinned browser twins; 5 are MV3 content scripts / `executeScript` payloads that cannot take an import |
| browser/js | 29 | `drawingApp.js` 1128 (its ~90 delegators serve the `window.stencil` facade `e2e/` drives), `chatPanel.js` 818 |
| desktop/src | 31 | from 64. What remains needs **extract-method**, not a move: a single function over 230 makes any new file illegal (`MainWindow::MainWindow` 565, `buildActions` 543, `eventFilter` 493) |
| desktop/tests | 32 | the 14 GUI area binaries, exempt by reason in the budget |

`mainWindow.hpp` (1547 lines, 51% comments) is the architectural item, not a size item: MOC
runs on the header, so splitting it is the risk that splitting method definitions across TUs
is not.

**Comment policy**: at most ~3 lines, and only what the code cannot say — an invariant, a
unit, a cross-surface coupling, a reason a value is what it is. No sprint or phase tags, no
`(user report)`, no "used to", no restating the next line.

---

## 5. Refactor discipline: pins, freeze, proof

**Pins before motion.** Before moving UI code, record what it currently looks like; after the
move, the pins must be byte-identical. A failing pin means the refactor changed behaviour —
**fix the code, not the pin.**

| Pin | What it captures |
|---|---|
| `e2e/pins/*.json` | computed styles + DOM shape for 21 browser/extension states |
| `desktop/tests/pins/stylesheets.txt` | 24 QSS sha256 hashes |
| `desktop/tests/pins/<platform>/*.png` | 24 renders at @1x and @2x |
| `cli/tests/pins/*.txt` | 20 TUI goldens |
| `browser/tests/pins/css.json`, `extension/tests/pins/css.json` | full CSS inventories |
| `bot|mcp|pystencil|server` goldens | user-facing text |

**UI freeze in refactor commits.** A commit that moves code changes no pixel and no string.
An intended visual change is its own commit, with the re-pin in it and nothing else.

**Prove it mechanically** (`tools/README.md` has the details):

- `node tools/moveCheck.mjs <gitRef> <path…>` — hashes every function/method body on both
  sides. A move of whole functions or data prints `LOST 0  NEW 0`. An **extract-class** does
  not and cannot: converting a method to a function rewrites the enclosing body (`this.app`
  becomes a parameter), so read the signal as **`LOST 0`, with only the enclosing wrapper
  NEW** — the landed `chatDock` split prints `LOST 1  NEW 2`.
- `node tools/commentOnlyDiff.mjs <gitRef> <path…>` — strips comments and normalizes
  whitespace; every file must print `OK`. Its `normalizeLines` is also the C++ fallback below.
- `desktop/tools/cppCommentDiff.sh <gitRef> <file…>` — the same for C++, via a real gcc
  preprocessor. **It exits 2 wherever `gcc` is the Apple clang shim, and a hand-rolled
  `gcc -fpreprocessed -dD -E -P` diff there emits two EMPTY files — a vacuous pass that looks
  exactly like success.** On macOS use `normalizeLines` from `commentOnlyDiff.mjs` over a
  multiset of lines, and assert the stripped text is non-empty before trusting it.

---

## 6. Pattern vocabulary

Use the name the repo already uses; don't introduce a synonym.

- **Facade over core** — `window.stencil` (browser), `CoreFacade` (desktop), `core.zig` (cli),
  `pystencil.core`. One narrow, guarded surface; every mutation goes through it, so console
  scripting, hotkeys, toolbar and LLM plans cannot diverge.
- **Mediator** — `DrawingApp` (browser) and `MainWindow` (desktop) wire collaborators to each
  other; the collaborators do not know one another.
- **Command** — every undoable operation is an entry on `historyStack` / `HistoryStack`,
  applied and reverted by the same code path.
- **Strategy** — LLM providers (one `wire` per provider) and image filters; selected by table
  lookup, never by a growing `if` chain.
- **Observer** — the event bus (`core/emitter.js`, Qt signals, the server's `internal/bus`).
- **Repository** — `projectsStore` / `internal/store` / `filestore`: persistence behind an
  interface the caller cannot see through.
- **Chain of Responsibility** — request middleware on the server, and the guard chains on
  each surface's fetch path.
- **Adapter** — CLI argv builders (`mcp/src/args.rs`, bot's `CliArgvBuilder`) translating a
  typed request into the CLI's documented flags.

---

## 7. Extension checklists

### Add an LLM op

1. `browser/js/config/llm/opRegistry.json` — the entry: `keys`, `profiles`, limits, `bullet`.
   Read `opRegistry.README.md` first; the key-spec language is expressive and you rarely need
   a native rule.
2. `cd browser && npm run gen-fixtures` — regenerates the mechanical fixture bundle. Add a
   hand-written fixture under `browser/js/config/llm/fixtures/opPlan/` for the interesting case.
3. Normalizer + executor, per surface in the op's profiles (**the validator is table-driven —
   you write no schema code**): browser `js/llm/opPlan.js` + `js/console/stencilApi.js`;
   desktop `src/llm/opPlan.cpp` + `src/llm/planExecutor.cpp`; cli `src/llm/opplan.zig`;
   mcp `src/opplan/{parse,lower,actions}.rs`; bot `Application/Llm/OpPlanParser*.cs` +
   `PromptService.Actions.cs`; pystencil `pystencil/llm.py`; extension `src/llm/opPlan.js`.
4. `llm-contract/llm-contract.md` (and the split §-files) — the normative prose.
5. Run every surface's fixture walker. They are the cross-language proof.

### Add a console command (cli, and its pystencil twin)

1. `cli/src/console/commands.zig` — the `Verb` enum member and its word match in the parser.
2. `cli/src/console/handlers.zig` — the handler. It returns values; it does not print.
3. `cli/src/console/ui.zig` / `screen.zig` — the rendering. Terminal output lives only here.
4. Help text, then re-record the TUI goldens: `STENCIL_UPDATE_PINS=1 zig build test`.
5. If the command is part of the shared console profile, mirror it in
   `pystencil/pystencil/cli.py`. Update `cli/README.md`.

### Add a desktop dialog

1. `desktop/src/dialogs/<name>Dialog.{hpp,cpp}` — chrome from `support/modalChrome.hpp`,
   reveal from `support/modalReveal.hpp`. Measure per-state heights in `showEvent`, not from
   a constructor `sizeHint` (a hidden widget's hint is stale).
2. Add the sources to `desktop/CMakeLists.txt`.
3. Wire the action in `desktop/src/app/mainWindowActions.cpp` and `mainWindowMenus.cpp`.
4. A shortcut goes in `browser/js/config/hotkeysConfig.json` — canonical — and reaches the
   desktop through the qrc alias.
5. Styling goes in the shared sheet in `support/theme.cpp`, never `setStyleSheet` on the
   widget.
6. Add a headless test under `desktop/tests/`, then re-pin:
   `STENCIL_UPDATE_UI_PINS=1` on the ui-pins target.

### Add an LLM provider

1. `browser/js/config/llm/providers.json` — id, `displayName`, `defaultBaseUrl`, `chatPath`,
   `wire`.
2. `llm-contract/llm-providers.md` — the wire mapping, normatively.
3. The mapping in each client, using its platform's built-in HTTP (**no new dependency**):
   browser `js/llm/llmClient.js`; extension `src/llm/llmClient.js`; desktop
   `src/llm/llmClient.cpp`; cli `src/llm/wire.zig` + `transport.zig`; mcp
   `src/llmtransport.rs`; bot `Infrastructure` ring, `Llm/HttpLlmClient.cs`; pystencil
   `pystencil/llm.py`; server `server/internal/llm/`.
4. Settings UI per surface (browser `js/llm/llmSettings.js`, desktop
   `dialogs/llmSettingsForm.cpp`, extension `src/llm/llmSettings.js`, cli `/llm`).
5. Fixtures under `browser/js/config/llm/fixtures/providerWire/`, plus each surface's walker.
6. An endpoint is **always explicit user configuration** — never discovered from fetched or
   scanned content. Keys live in env, never in a URL. See `.claude/rules/security.md`.
