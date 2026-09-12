# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

Stencil is an image-annotation / drawing tool: **one shared C++ logic core (`core/`) feeding
four front-ends**, plus four adapters that wrap the CLI or the collaboration server.

| Tree | What | Runs `core/`? |
|---|---|---|
| `core/` | C++17, STL-only, GUI-free logic: formulas, geometry, color, page metrics, crop, raster, history, projects | — |
| `browser/` | vanilla ES-module JS app, no build step — `core/` as wasm, with a JS fallback | yes (wasm) |
| `desktop/` | C++17 + Qt 6 app — links `core/` via `add_subdirectory(../core)` | yes (linked) |
| `cli/` | Zig tool — **recompiles** `core/` and drives it over `extern "C"` | yes (recompiled) |
| `pystencil/` | stdlib-only Python package — **recompiles** `core/`, drives it via ctypes | yes (recompiled) |
| `extension/` | Chrome MV3 — scans page images, hands them to `browser/` via a URL fragment | no |
| `mcp/` | Rust MCP server — shells out to the `cli/` binary | no |
| `server/` | Go collaboration server — projects + live multi-client sessions over REST/WS/TCP; Postgres + a secured file store | no |
| `bot/` | .NET Telegram bot (clean architecture) — wraps the CLI + server REST | no |
| `e2e/` | Node/Playwright cross-surface smoke harness over the real wire protocols | no |

`extension/`, `mcp/`, `server/`, `bot/` and `e2e/` are adapters or black-box harnesses: the
parity contract below does **not** reach them. `mcp/` and `bot/` depend on the CLI's argv
contract and its `wrote {path} ({w}x{h})` / `error:` stderr output; `server/`'s contract is
`server/internal/protocol`. The desktop's own GUI e2e is a QtTest target
(`desktop/tests/MainWindow.<area>.gui.cpp`), not in `e2e/`.

## Commands

No dependencies to install anywhere. JS suites use Node's built-in runner; C++ uses CMake +
Doctest; each other surface uses its platform's default.

| Subproject | Build | Test | Run / single test |
|---|---|---|---|
| **browser** | none to run; optional `npm run build` → single-file `stencil.html` | `cd browser && npm test` | `npm run serve` (http://localhost:8080); single: `node --test tests/<file>.test.js` |
| **core** | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && cmake --build core/build -j` | `ctest --test-dir core/build --output-on-failure` | `core/build/stencil_tests -tc="<case>"` (only a few files use `TEST_SUITE`, so `-ts=` reaches only those) |
| **desktop** | `cd desktop && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | `ctest --test-dir build --output-on-failure` (needs Qt; headless) | `./build/stencil`; single: `ctest -R <name>` |
| **cli** | `cd cli && zig build` (→ `zig-out/bin/stencil`) | `zig build test --summary all` | `zig build run -- --help` |
| **pystencil** | `cd pystencil && python3 build.py` (needs a C++17 compiler) | `python3 -m unittest discover -s tests` | `python3 -m pystencil --help` |
| **extension** | none | `cd extension && npm test` | load unpacked at `chrome://extensions` (needs `browser/` served) |
| **mcp** | `cd mcp && cargo build` (→ `target/debug/stencil-mcp`) | `cargo test` (e2e self-skips without the CLI binary) | `claude mcp add stencil -- $(pwd)/target/debug/stencil-mcp` |
| **server** | `cd server && go build ./...` | `go test ./...`; `go test -race ./internal/hub/...` (store/bus e2e self-skip without `TEST_DATABASE_URL`/`REDIS_URL`) | `go run ./cmd/stencil-server`; needs `DATABASE_URL` — see the sample env file in `server/` |
| **bot** | `cd bot && dotnet build Stencil.TelegramBot.slnx` | `dotnet test Stencil.TelegramBot.slnx` (offline: no token, server, CLI or Redis) | `dotnet run --project src/Stencil.TelegramBot.Bot` (needs `TELEGRAM_BOT_TOKEN` + the CLI) |
| **e2e** | `cd e2e && npm ci && npx playwright install chromium` | `npm test` | `npm run test:ui`; full stack: `E2E_STACK=1 npm test` (docker compose) |

- `node --test` **never loads wasm** — it always runs the JS fallback path.
- **wasm build** (needs Emscripten): `cd browser && npm run build-wasm` → the gitignored
  `browser/js/wasm/stencilCore.js`.
- After editing `browser/js/config/llm/opRegistry.json`: `cd browser && npm run gen-fixtures`
  (the browser walker fails while the generated bundle is stale).
- The browser app must be served over HTTP (ES modules refuse `file://`). Override with
  `ADDR=0.0.0.0 PORT=3000 npm run serve`.
- Docker images compile `core/`, so **build from the repo root** with `-f`:
  `docker build -f browser/Dockerfile -t stencil-browser .`.

## The parity contract (the most important thing to know)

1. **Each `core/` module is a port of a specific `browser/js/` call site** — the mapping is at
   the top of each core header — and must stay behaviorally identical down to edge cases.
   `core/tests/` are ports of `browser/tests/`. Change one side, change the other, update both.
2. **The browser runs `core/` via wasm with a JS fallback that must match it op-for-op.**
   `browser/tests/wasm-parity.test.js` enforces it; CI builds wasm fresh to run it.
3. **No `eval` anywhere.** `browser/js/core/formulaEngine.js` and `core/parse/formulaParser`
   are both real recursive-descent parsers (no `new Function`), aligned down to the shared
   `MAX_DEPTH`.
4. **Three source lists.** Adding/removing/renaming a `core/*.cpp` means editing
   `STENCIL_CORE_SOURCES` (`core/CMakeLists.txt`), the array in `cli/build.zig`, **and** the
   list in `pystencil/build.py`.
5. **`core/` is STL-only, codec-free, GUI-free.** Codecs, HTTP, JSON, video, rendering,
   persistence and the event loop belong to the adapters.
6. **`browser/js/config/` is the canonical home** for every shared data table; other surfaces
   embed it (qrc alias / `@embedFile` / `include_str!` / `<EmbeddedResource>`) or keep a
   drift-tested copy.

## Where to read next

- **`ARCHITECTURE.md`** — the design rulebook: layer model per app, the shared-data rails,
  size/comment budgets, refactor discipline (pins, UI freeze, move proofs), the pattern
  vocabulary, and checklists for adding an op / command / dialog / provider.
- **`llm-contract/`** — the normative LLM contract (`llm-contract.md` plus `llm-providers.md`,
  `llm-profiles.md`, `llm-chat.md`; §1–§13 are stable across the set). It is machine-readable
  and test-guarded in `browser/js/config/llm/`. The LLM lives **entirely in the adapters** —
  `core/` has no LLM code.
- **`.claude/rules/`** — auto-loaded agent rules: `security.md`, `no-dependencies.md`,
  `architecture.md`, `tests.md`, and `core-changes.md` (path-scoped to `core/`).
- **Each subproject's `README.md`** — read the relevant one before working in that tree.
- **`tools/README.md`** — `moveCheck.mjs` and `commentOnlyDiff.mjs`.
