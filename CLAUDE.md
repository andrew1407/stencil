# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

Stencil is an image-annotation / drawing tool: **one shared C++ logic core (`core/`) feeding
four front-ends**, plus a family of adapters that wrap the CLI or the collaboration server.

| Tree | What | Runs `core/`? |
|---|---|---|
| `core/` | C++17, STL-only, GUI-free logic: formulas, the `.stc` script engine, geometry, color, page metrics, crop, raster, history, projects | — |
| `browser/` | vanilla ES-module JS app, no build step — `core/` as wasm, with a JS fallback | yes (wasm) |
| `desktop/` | C++17 + Qt 6 app — links `core/` via `add_subdirectory(../core)` | yes (linked) |
| `cli/` | Zig tool — **recompiles** `core/` and drives it over `extern "C"` | yes (recompiled) |
| `pystencil/` | stdlib-only Python package — **recompiles** `core/`, drives it via ctypes | yes (recompiled) |
| `browser-extension/` | Chrome MV3 — scans page images, hands them to `browser/` via a URL fragment | no |
| `vscode-extension/` | VS Code editor support for `.stc`, `.stcjs` and `.pystc` (and a `.stencil` project icon) — spawns the `cli/` binary (including `--script-emit`) and the Python that runs a `.pystc`, hands scripts to `browser/` over `#stencil=` or drives its console through VS Code's built-in JS debugger, and carries a byte-equal copy of `browser/js/core/script/` for in-editor parsing | no |
| `mcp/` | Rust MCP server — shells out to the `cli/` binary | no |
| `server/` | Go collaboration server — projects + live multi-client sessions over REST/WS/TCP; Postgres + a secured file store | no |
| `bot/` | .NET Telegram bot (clean architecture) — wraps the CLI + server REST | no |
| `e2e/` | Node/Playwright cross-surface smoke harness over the real wire protocols | no |

`browser-extension/`, `vscode-extension/`, `mcp/`, `server/`, `bot/` and `e2e/` are adapters or
black-box harnesses: the parity contract below does **not** reach them. `mcp/`, `bot/` and
`vscode-extension/` depend on the CLI's argv contract and its `wrote {path} ({w}x{h})` /
`error:` stderr output; `server/`'s contract is `server/internal/protocol`. The desktop's own
GUI e2e is a QtTest target (`desktop/tests/MainWindow.<area>.gui.cpp`), not in `e2e/`.

## Commands

No dependencies to install anywhere except the two sanctioned dev-only ones (`browser/`'s
`vite`, `vscode-extension/`'s `@vscode/vsce`). JS suites use Node's built-in runner; C++ uses
CMake + Doctest; each other surface uses its platform's default.

| Subproject | Build | Test | Run / single test |
|---|---|---|---|
| **browser** | none to run; optional `npm run build` → single-file `stencil.html` | `cd browser && npm test` | `npm run serve` (http://localhost:8080); single: `node --test tests/<file>.test.js` |
| **core** | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && cmake --build core/build -j` | `ctest --test-dir core/build --output-on-failure` | `core/build/stencil_tests -tc="<case>"` (only a few files use `TEST_SUITE`, so `-ts=` reaches only those) |
| **desktop** | `cd desktop && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` | `ctest --test-dir build --output-on-failure` (needs Qt; headless) | `./build/stencil`; single: `ctest -R <name>` |
| **cli** | `cd cli && zig build` (→ `zig-out/bin/stencil`) | `zig build test --summary all` | `zig build run -- --help` |
| **pystencil** | `cd pystencil && python3 build.py` (needs a C++17 compiler) | `python3 -m unittest discover -s tests` | `python3 -m pystencil --help` |
| **browser-extension** | none | `cd browser-extension && npm test` | load unpacked at `chrome://extensions` (needs `browser/` served) |
| **vscode-extension** | none to run it from source (open the folder, press F5); `npm ci && npm run package` → `stencil-stc.vsix` | `cd vscode-extension && npm test` | `code --install-extension stencil-stc.vsix`, then open any `.stc` |
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
- After editing `browser/js/core/script/`: re-copy it into `vscode-extension/src/parser/script/`
  (`parserParity.test.js` pins them byte-for-byte, both directions).

## The parity contract (the most important thing to know)

1. **Each `core/` module is a port of a specific `browser/js/` call site** — the mapping is at
   the top of each core header — and must stay behaviorally identical down to edge cases.
   `core/tests/` are ports of `browser/tests/`. Change one side, change the other, update both.
2. **The browser runs `core/` via wasm with a JS fallback that must match it op-for-op.**
   `browser/tests/wasm/wasm-parity.test.js` enforces it; CI builds wasm fresh to run it.
3. **No `eval` anywhere.** `browser/js/core/parse/formulaEngine.js` and `core/parse/formulaParser`
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

- **`ARCHITECTURE.md`** — the system design: the system map, the core parity contract,
  the shared-data rails, the layer model per app and the pattern vocabulary.
- **`contracts/`** — the normative contracts, one directory each.
  - **`llm/`** — the LLM contract (`llm-contract.md` plus `llm-providers.md`,
    `llm-profiles.md`, `llm-chat.md`; §1–§13 are stable across the set). Machine-readable and
    test-guarded in `browser/js/config/llm/`. The LLM lives **entirely in the adapters** —
    `core/` has no LLM code.
  - **`stc/`** — the `.stc` script language: lexis, directives, units, templates, undo, the
    error catalogue and the per-surface execution table. Parsed and lowered in `core/script/`,
    proved by the corpus in `browser/js/config/script/fixtures/cases.txt`.
- **`.claude/rules/`** — auto-loaded agent rules: `security.md`, `no-dependencies.md`,
  `architecture.md`, `tests.md`, `checklists.md` (the file-by-file steps for adding an op /
  console command / dialog / provider / script directive), and `core-changes.md` (path-scoped
  to `core/`).
- **Each subproject's `ARCHITECTURE.md`** — **normative for that tree.** Before changing a
  surface, read its `ARCHITECTURE.md` and keep the change inside its layers, placement
  table and rules. Each is an independent document of that surface's design, in the same
  seven sections — Layers, Where things go, Entities, Patterns, Design, Rules, Tests — and
  stays current with the tree.
- **Each subproject's `README.md`** — the user-facing guide only: what it is, build, run,
  test, configure, use. No architecture, internals or feature inventories.
- **`usecases/docs/<app>/USECASES.md`** — the illustrated walkthrough of an app: scenarios and
  steps with screenshots/GIFs under `usecases/docs/<app>/img/`, generated by the scripts in
  `usecases/capture-runner/` (never hand-edited). User-facing like a README: no architecture, no
  inventories, no counts. Docs-only: touching `usecases/` runs no CI job.
- **`tools/README.md`** — `moveCheck.mjs` and `commentOnlyDiff.mjs`.
