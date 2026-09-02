# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Stencil is an image-annotation / drawing tool shipped as **one shared C++ logic core (`core/`) feeding four front-ends**: a browser app (vanilla ES modules), a desktop app (Qt 6), a CLI (Zig), and a companion Chrome extension (MV3) that feeds images into the browser editor. A fifth subproject, an **MCP server (`mcp/`, Rust)**, wraps the CLI rather than the core; a sixth, a **collaboration server (`server/`, Go)**, stores/shares projects and hosts live multi-client edit sessions over its own WS/TCP protocol; a seventh, a **stdlib-only Python package (`pystencil/`)**, recompiles the core and drives it via ctypes; and an eighth, a **Telegram bot (`bot/`, .NET)**, wraps the CLI + server REST behind a chat UI. Each subproject has its own README with deeper detail; read the relevant one before working in it.

```
core/        C++17, STL-only, GUI-free shared logic (formulas, geometry, color, page metrics, crop, raster, history, projects)
browser/     vanilla ES-module JS app, no build step — runs core/ compiled to WebAssembly, with a JS fallback
desktop/     C++17 + Qt 6 app — links core/ via add_subdirectory(../core)
cli/         Zig tool — recompiles core/ sources and drives them over an extern "C" ABI
extension/   Chrome MV3 extension — scans page images and hands them to browser/ via a URL fragment
mcp/         Rust MCP server — shells out to the cli/ binary; depends on the CLI's command contract, NOT on core/
server/      Go collaboration server — stores/shares projects + live multi-client edit sessions over WS/TCP; Postgres + a secured file store, NOT on core/
pystencil/   stdlib-only Python package — recompiles core/ sources and drives them over the extern "C" ABI via ctypes
bot/         .NET Telegram bot (clean architecture) — shells out to the cli/ binary + speaks server/ REST, NOT on core/
e2e/         Node/Playwright cross-surface smoke harness — drives the REAL browser app, extension, and server binary; NOT on core/
```

`mcp/`, `server/`, `bot/` and `e2e/` are thin protocol adapters / black-box harnesses, not core consumers: they never link/recompile `core/`, so the parity contract below (STL-only core, source-list sync, wasm/JS-fallback alignment) does **not** extend to them. `e2e/` runs the built artifacts end-to-end over their real wire protocols (one representative flow per surface); the **desktop Qt** app's own end-to-end test is separate — it lives with the desktop build as a QtTest target (`desktop/tests/mainWindow.gui.cpp`), not in `e2e/`. `mcp/`'s only contract is the CLI's documented flags and its `wrote {path} ({w}x{h})`/`error:` stderr output. `bot/` shares that same CLI contract (it ports `mcp/`'s argv/outcome adapters) plus the server REST routes it ports from `pystencil`. `server/`'s contract is its REST + WebSocket/TCP wire protocol (`server/internal/protocol`), which the four front-ends mirror to connect, list/share projects, and edit collaboratively; it persists metadata in Postgres and bytes in a custom secured file store, and never touches `core/`.

## Commands

All JS test suites use Node's built-in runner (no deps to install). C++ uses CMake + Doctest. CLI uses Zig.

| Subproject | Build | Test | Run / single test |
|---|---|---|---|
| **browser** | none (ES modules) | `cd browser && npm test` | `npm run serve` (→ http://localhost:8080); single: `node --test tests/<file>.test.js` |
| **core** | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && cmake --build core/build -j` | `ctest --test-dir core/build --output-on-failure` | single suite: `core/build/stencil_tests -ts=<suite>` (Doctest) |
| **desktop** | `cd desktop && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | `ctest --test-dir build --output-on-failure` (needs Qt; runs headless crop/image tests) | `./build/stencil` |
| **cli** | `cd cli && zig build` (→ `zig-out/bin/stencil`) | `zig build test --summary all` | `zig build run -- --help` |
| **mcp** | `cd mcp && cargo build` (→ `target/debug/stencil-mcp`) | `cargo test` (e2e tests self-skip without the CLI binary) | `claude mcp add stencil -- $(pwd)/target/debug/stencil-mcp` |
| **extension** | none | `cd extension && npm test` | load unpacked at `chrome://extensions` (needs `browser/` served) |
| **server** | `cd server && go build ./...` (→ `go run ./cmd/stencil-server`) | `go test ./...` (store/redisbus e2e self-skip without `TEST_DATABASE_URL`/`REDIS_URL` — the store tests truncate, so they never read `DATABASE_URL`; `go test -race ./internal/hub/...`) | needs Postgres (`DATABASE_URL`) + optional Redis (`REDIS_URL`); see `server/.env.example` |
| **bot** | `cd bot && dotnet build Stencil.TelegramBot.slnx` | `dotnet test Stencil.TelegramBot.slnx` (offline: no token/server/CLI/Redis) | `dotnet run --project src/Stencil.TelegramBot.Bot` (needs `TELEGRAM_BOT_TOKEN` in `bot/.env` + the CLI) |

- `node --test` **never loads wasm** — it always runs the JS fallback path.
- The browser app must be served over HTTP (ES modules refuse `file://`). Override host/port: `ADDR=0.0.0.0 PORT=3000 npm run serve`.
- **WebAssembly build** (needs Emscripten on `PATH`): `cd browser && npm run build-wasm` — compiles `core/` and drops the generated `js/wasm/stencilCore.js` (gitignored, not committed).
- Docker images compile `core/`, so **build from the repo root** with `-f`: `docker build -f browser/Dockerfile -t stencil-browser .` (likewise `cli/Dockerfile`).

## Architecture: the parity contract (most important thing to know)

The four front-ends **deliberately mirror each other**, and three of them run the *same* `core/` C++ code. The hard rules that this creates — and that you must preserve when editing — are:

1. **Each `core/` module is a port of a specific browser JS call site** and must stay behaviorally identical down to edge cases. The mapping is noted at the top of each core header (e.g. `core/geometry` ← `browser/js/utils.js`, `core/page/pageMetrics` ← `drawingApp.js`, `core/state/historyStack` ← `historyStack.js`). The C++ tests are themselves ports of `browser/tests/`. If you change behavior on one side, change it on the other and update both test suites.

2. **The browser runs `core/` via wasm, with a JS fallback that must match it.** At boot `browser/js/index.js` calls `core.init()` (`js/core/stencilCore.js`); each pure-logic JS module delegates to wasm when loaded and keeps its JS body as the fallback used when wasm isn't built or fails. `browser/tests/wasm-parity.test.js` asserts the compiled core agrees with the JS reference op-for-op (CI builds wasm fresh and runs this — it fails if behavior diverges). Keep wasm and JS fallback behaviorally aligned.

3. **No `eval` anywhere.** `browser/js/core/formulaEngine.js` and `core/parse/formulaParser` are both real recursive-descent parsers (no `new Function`/`eval`): `+ - * / ** ( )`, single variable, `**` right-associative, empty = identity, div-by-zero/overflow = invalid, recursion capped at the same depth on both sides (`MAX_DEPTH` ↔ `kMaxDepth`). The JS parser is the wasm fallback and reference; keep the two aligned op-for-op (same operators, precedence, identity-on-error).

4. **The CLI and the Python package recompile core sources, they do not link the CMake library.** The file list in `cli/build.zig` **and** the source list in `pystencil/build.py` **must stay in sync** with `STENCIL_CORE_SOURCES` in `core/CMakeLists.txt`. Adding/removing/renaming a core `.cpp` means editing all three.

5. **`core/` is STL-only, codec-free, GUI-free.** It moves bytes and numbers over flat `double*`/RGBA8 buffers + C strings (`core/wasmApi.cpp` for the browser, `core/cliApi.{h,cpp}` for the CLI — no embind, no host allocation). Everything platform-specific lives in the adapters: image codecs/HTTP/video/JSON are the Zig CLI's job; QImage/canvas rendering, persistence, and the event loop are the GUI apps'. Don't pull Qt, a codec, or DOM access into `core/`.

## Browser app internals

- Single entrypoint: `index.html` loads only `js/index.js`; the ES-module graph pulls in everything else. No bundler.
- `js/core/` holds `DrawingApp` and its collaborators (renderer, storage, history, zoom/pan, coord table, formulas, projects store). `js/ui/` are pure string-returning components composed by `layout()`. `js/config/` holds constants + the hotkey/help-text registries. `js/worker/` is the cross-tab projects-sync worker.
- The editor exposes a frozen, hard-guarded scripting facade on `window.stencil` (`js/console/stencilApi.js`); every mutation routes through the same core methods the toolbar uses, so console scripting and UI stay in sync. See `browser/README.md` for the full surface.
- The extension hands off images via the URL **fragment** (`#stencil=<encodeURIComponent(JSON)>`), consumed by `DrawingApp.applyExternalLaunch()` — the fragment never reaches the server.

## LLM / AI-assistant support

Stencil has an LLM assistant across its surfaces, built **entirely in the adapters** —
`core/` has no LLM code and the parity contract above is unaffected. The single source of
truth is **`llm-contract/llm-contract.md`** (plus its split-out sections `llm-contract/llm-providers.md`,
`llm-contract/llm-profiles.md`, `llm-contract/llm-chat.md` — section numbers §1–§13 are stable across the
set): the *op-plan* JSON the model must answer with (ops map 1:1 onto existing operations —
crop specs, quarter rotates, filters/tint, layout `Line`s, formulas, page formats, blanks,
video frames; validated strictly before anything executes), the provider config (+ defaults:
Ollama `http://localhost:11434`, OpenAI-compatible/LM Studio `http://localhost:1234/v1`, or
Anthropic proxied by a collaboration server), the canonical system prompt, and the
per-provider wire mappings. The exhaustive detail is machine-readable and test-guarded:
`browser/js/config/llm/opRegistry.json` (ops/profiles/limits), `systemPrompt.json` (the
prompt prose; ops bullets are registry-generated), `providers.json` (provider constants),
and the conformance fixtures under `browser/js/config/llm/fixtures/` +
`browser/js/config/fixtures/`, walked by every surface's tests. Every client implements the
contract with its platform's built-in HTTP (no new deps) and mocks it in tests — change the
contract and you change every implementation listed in its table, same rule as the core
parity contract.

Per surface: **browser** — dockable/floating chat panel (`js/llm/`, executed through the
`window.stencil` facade); **desktop** — floatable chat `QDockWidget` (`src/llm/`,
transport-injectable client); **extension** — chat over the page's scanned images with its
own op profile (contract §8: `focus`/`open`/`attach`; editing happens after the `#stencil=`
handoff); **cli** and **pystencil** consoles — `/prompt` (`/p`) + `/llm` config commands;
**pystencil** API — `Editor.prompt()` / `Chat` in `pystencil/llm.py`; **bot** — `/prompt`
(`/p`) with variants sent as media groups, plus `/chat` chat mode (plain messages route to the
same prompt path); **mcp** — `stencil_prompt` tool (hand-rolled
plain-http transport, per the no-dependencies rule). The **server** proxies any of the three
providers at `POST /llm/chat` / `GET /llm/info` behind the normal bearer auth — `LLM_API_KEY`
lives only in its env (`LLM_PROVIDER`, `LLM_MODEL`, `LLM_BASE_URL`, `LLM_MAX_TOKENS`,
`LLM_TIMEOUT_SECONDS` alongside; `ANTHROPIC_API_KEY` is a legacy alias honoured only for
`anthropic`), with `LLM_RATE_PER_MINUTE`/`LLM_MAX_IN_FLIGHT` bounding what one session can
spend — and its per-project file kinds extend to `video` + `variant1..8` + `chat`
(filestore-only in v1; `chat` holds the contract-§12 opt-in per-project chat transcript,
deletable via `DELETE /projects/{id}/files/{kind}`). Config env keys elsewhere are the shared `STENCIL_LLM_*` family. LLM endpoints are
always explicit user configuration (never discovered from content), and model output is
data — plans are validated against the whitelisted op set, never trusted.

## Doctest / dependency notes

- Doctest is a single header (pinned v2.4.11) fetched into `core/third_party/doctest.h` at configure time with SHA-256 verification — not committed, nothing to install.
- `core/wasmApi.cpp` is plain STL, so it's also compiled natively into `stencil_tests` and exercised without `emcc`.
- The desktop build turns the core's Doctest suite **off** (`STENCIL_CORE_BUILD_TESTS`) and defers to the dedicated core build; it registers its own Qt offscreen ctest cases.

## CI

`.github/workflows/ci.yml` runs thirteen independent jobs on push/PR to `main`: browser (JS), extension (JS), core (C++ + Doctest), desktop (Qt + headless tests, **including the `stencil_mainwindow_gui` QtTest e2e**), **wasm** (builds the core fresh with Emscripten and runs the parity test against it), cli (Zig), **pystencil** (Python stdlib tests; recompiles core via `build.py`), mcp (Rust, builds the CLI first for its gated e2e), **server** (Go build + `go test -race`, with Postgres + Redis service containers for the gated store/bus integration tests), **bot** (.NET build + the offline xUnit suite for the Telegram bot), **guard-hook** (the `.claude/hooks` PreToolUse guard's own `node --test` suite), **e2e** (Node/Playwright — brings up db+redis+server via docker compose and drives the real browser app + unpacked extension + server binary over their wire protocols; see `e2e/README.md`), and **docker-images** (a matrix that `docker build`s all five shipped Dockerfiles — browser/cli/mcp/bot from the repo root, server from `./server` — so a broken image never ships; behavior is covered elsewhere, this guards packaging). The wasm job is what catches core/JS-fallback divergence. `release.yml` builds desktop packages for macOS/Windows/Linux on `v*` tags.

## AI harness rules

Project rules for AI agents live in `.claude/rules/` (auto-loaded; path-scoped ones fire
when you touch matching files):

- `security.md` — driving Stencil's network-facing surfaces safely. Enforced by a
  cross-platform PreToolUse hook (`.claude/hooks/guard.mjs`) that blocks secret reads and
  exfil-shaped commands/scripts and asks before out-of-repo or shared-project writes, backed
  by a `deny` list in `.claude/settings.json`.
- `no-dependencies.md` — this repo adds no deps, bundlers, or build steps; use the built-in
  tooling.
- `core-changes.md` (scoped to `core/`, `cli/build.zig`, `pystencil/build.py`) — the 3-file
  source-list sync and the wasm/JS parity obligation.
