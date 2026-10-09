# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

## What this is

Stencil is an image-annotation / drawing tool: **one shared C++ logic core (`core/`) feeding
four front-ends**, plus a family of adapters that wrap the CLI or the collaboration server.

| Tree | What | Runs `core/`? |
|---|---|---|
| `core/` | C++17, STL-only, GUI-free logic: formulas, the `.stc` script engine, the op-plan validator (over its own JSON reader), geometry, color, page metrics, crop, raster, history, projects | — |
| `browser/` | vanilla ES-module JS app, no build step — `core/` as wasm, with a JS fallback | yes (wasm) |
| `desktop/` | C++17 + Qt 6 app — links `core/` via `add_subdirectory(../core)` | yes (linked) |
| `cli/` | Zig tool — **recompiles** `core/` and drives it over `extern "C"` | yes (recompiled) |
| `pystencil/` | stdlib-only Python package — **recompiles** `core/`, drives it via ctypes | yes (recompiled) |
| `browser-extension/` | Chrome MV3 — scans page images, hands them to `browser/` via a URL fragment | no |
| `vscode-extension/` | VS Code editor support for `.stc`, `.stcjs` and `.pystc` (and a `.stencil` project icon) — spawns the `cli/` binary (including `--script-emit`) and the Python that runs a `.pystc`, hands scripts to `browser/` over `#stencil=` or drives its console through VS Code's built-in JS debugger, and carries a byte-equal copy of `browser/js/core/script/` for in-editor parsing | no |
| `mcp/` | Rust MCP server — shells out to the `cli/` binary | no |
| `server/` | Go collaboration server — projects over REST, a live events feed over WS/TCP; Postgres + a secured file store | no |
| `bot/` | .NET Telegram bot (clean architecture) — wraps the CLI + server REST | no |
| `e2e/` | Node/Playwright cross-surface smoke harness over the real wire protocols | no |
| `tools/` | Node repo checks: move proofs, dead doc/comment paths, twin sync, size caps (`tools/README.md`) | no |

`browser-extension/`, `vscode-extension/`, `mcp/`, `server/`, `bot/` and `e2e/` are adapters or
black-box harnesses: the parity contract below does **not** reach them. `mcp/`, `bot/` and
`vscode-extension/` depend on the CLI's argv contract and its `wrote {path} ({w}x{h})` /
`error:` stderr output; `server/`'s contract is `server/internal/protocol`. The desktop's own
GUI e2e is QtTest targets (`MainWindow.<area>.gui.cpp` under `desktop/tests/`), not in `e2e/`.

## Commands

No dependencies to install anywhere except the two sanctioned dev-only ones (`browser/`'s
`vite`, `vscode-extension/`'s `@vscode/vsce`). JS suites use Node's built-in runner; C++ uses
CMake + Doctest; each other surface uses its platform's default.

| Subproject | Build | Test | Run / single test |
|---|---|---|---|
| **browser** | none to run; optional `npm run build` → single-file `stencil.html` | `cd browser && npm test` | `npm run serve` (http://localhost:8080/); single: `node --test tests/<file>.test.js` |
| **core** | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && cmake --build core/build -j 4` | `ctest --test-dir core/build --output-on-failure` | `core/build/stencil_tests -tc="<case>"` (only a few files use `TEST_SUITE`, so `-ts=` reaches only those) |
| **desktop** | `cd desktop && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j 4` | `ctest --test-dir build --output-on-failure` (needs Qt; headless) | `./build/stencil`; single: `ctest -R <name>` |
| **cli** | `cd cli && zig build` (→ `zig-out/bin/stencil`) | `zig build test --summary all` | `zig build run -- --help` |
| **pystencil** | `cd pystencil && python3 build.py` (needs a C++17 compiler; also writes `pystencil/pystencil/_data/` from `common/config/`) | `python3 -m unittest discover -s tests` | `python3 -m pystencil --help` |
| **browser-extension** | none | `cd browser-extension && npm test` | load unpacked at `chrome://extensions` (needs `browser/` served) |
| **vscode-extension** | none to run it from source (open the folder, press F5); `npm ci && npm run package` → `stencil-stc.vsix` | `cd vscode-extension && npm test` | `code --install-extension stencil-stc.vsix`, then open any `.stc` |
| **mcp** | `cd mcp && cargo build` (→ `target/debug/stencil-mcp`) | `cargo test` (e2e self-skips without the CLI binary) | `claude mcp add stencil -- $(pwd)/target/debug/stencil-mcp` |
| **server** | `cd server && go generate ./... && go build ./...` (generate copies `common/config/` into the embedded assets) | `go test ./...`; `go test -race ./...` (store/bus e2e self-skip without `TEST_DATABASE_URL`/`REDIS_URL`) | `go run ./cmd/stencil-server`; needs `DATABASE_URL` — see the sample env file in `server/` |
| **bot** | `cd bot && dotnet build Stencil.TelegramBot.slnx` | `dotnet test Stencil.TelegramBot.slnx` (offline: no token, server, CLI or Redis) | `dotnet run --project src/Stencil.TelegramBot.Bot` (needs `TELEGRAM_BOT_TOKEN` + the CLI) |
| **e2e** | `cd e2e && npm ci && npx playwright install chromium` | `npm test` | `npm run test:ui`; full stack: `E2E_STACK=1 npm test` (docker compose) |
| **tools** | none | `node --test tools/*.test.mjs` | `node tools/docPaths.mjs --check`, `node tools/syncTwins.mjs --check` |

- `node --test` **never loads wasm** — it always runs the JS fallback path.
- **wasm build** (needs Emscripten): `cd browser && npm run build-wasm` → the gitignored
  `browser/js/wasm/stencilCore.js`.
- After editing `common/config/llm/opRegistry.json`: `cd browser && npm run gen-fixtures`
  (it rewrites `common/fixtures/llm/opPlan/generated/`; the browser walker fails while that bundle
  is stale), then `node tools/syncTwins.mjs`, which also refreshes the copies the Chrome
  extension and the `.vsix` ship.
- pystencil's package data and the server's embedded assets are **generated from `common/`**, never
  committed: `cd pystencil && python3 build.py` writes the first, `cd server && go generate ./...`
  the second (the Dockerfiles, CI and the verify matrix run it before building).
- The browser app must be served over HTTP (ES modules refuse `file://`). `npm run serve` serves
  the app at `/` and `common/` at `/common/`, where its relative `common/` imports land. Override
  with `ADDR=0.0.0.0 PORT=3000 npm run serve`.
- Docker images compile `core/`, so **build from the repo root** with `-f`:
  `docker build -f browser/Dockerfile -t stencil-browser .`.
- A byte-equal port or drift-tested copy (`vscode-extension/src/parser/script/`, the
  extension's `src/lib/` ports, the config copies in `pystencil/pystencil/_data/` …) is never
  edited in place: edit the original, then `node tools/syncTwins.mjs` re-copies every twin
  listed in `tools/twins.json` (`--check` only reports).
- The CLI builds with Zig 0.16 (`cli/build.zig.zon` `minimum_zig_version`); a newer Zig fails
  inside `build.zig` on the changed build API.
- `zig build` where every SDK `xcrun` offers is newer than Zig supports (`INFINITY`
  undeclared) needs a libc file naming an older SDK still installed:
  `zig libc | sed -E 's#MacOSX[0-9.]*\.sdk#MacOSX<older>.sdk#' > "$TMPDIR/zig-libc.txt"`, then
  `ZIG_LIBC="$TMPDIR/zig-libc.txt" zig build`.

## Commits

One short subject line (about 50–70 characters), no body, no `Co-Authored-By` trailer; a piece
of work lands on `main` as a single commit.

## The parity contract (the most important thing to know)

Stated in full in `ARCHITECTURE.md` §1–§2; `.claude/rules/core-changes.md` loads with any
`core/` edit. The index:

1. **Each `core/` module ports a named `browser/js/` call site** (mapped in its header) — change
   one side, change the other, and both test suites.
2. **The browser's JS fallback matches wasm op-for-op** — `browser/tests/wasm/` proves it.
3. **No `eval` anywhere** — both formula parsers are recursive descent, capped at one `MAX_DEPTH`.
4. **A new, renamed or removed `core/*.cpp` edits three source lists**; a new core folder three
   include-dir lists; a new wasm export `EXPORTED_FUNCTIONS`.
5. **`core/` is STL-only, codec-free, GUI-free, and never throws** — wasm has no exceptions.
6. **`common/` is the canonical home** of everything two or more surfaces share — tables in
   `common/config/`, corpora in `common/fixtures/`, brand art, samples. Other surfaces embed or
   read it, the browser serves it at `/common/`, and a package that cannot reach outside
   its folder keeps a drift-tested copy (the parser copy byte-equal except the import specifiers
   `tools/twins.json` `rewrite` declares).

## Where to read next

- **`ARCHITECTURE.md`** — the system design: the system map, the core parity contract,
  the shared-data rails, the layer model per app and the pattern vocabulary.
- **`contracts/`** — the normative contracts, one directory each.
  - **`llm/`** — the LLM contract (`llm-contract.md` plus `llm-providers.md`,
    `llm-profiles.md`, `llm-chat.md`; §1–§13 are stable across the set). Machine-readable and
    test-guarded in `common/config/llm/`. Model calls live **entirely in the adapters**;
    `core/opplan/` only validates the plan a model returned (mcp and bot via `--plan-check`).
  - **`stc/`** — the `.stc` script language: lexis, directives, units, templates, undo, the
    error catalogue and the per-surface execution table. Parsed and lowered in `core/script/`,
    proved by the corpus in `common/fixtures/script/cases.txt`.
- **`.claude/rules/`** — auto-loaded agent rules: `security.md`, `no-dependencies.md` and
  `architecture.md` always; the path-scoped `core-changes.md` (`core/`), `desktop-qt.md`
  (`desktop/`), `tests.md` (every test tree) and `docs.md` (the surface docs) with their files.
- **`.claude/skills/`** — on-demand procedures: `add-llm-op`, `add-llm-provider`,
  `add-stc-directive`, `add-console-command`, `add-desktop-dialog` (the file-by-file steps),
  `split-move` (splitting, moving and renaming, with the proofs), `verify` (the full matrix)
  and `stencil` (driving the CLI). The read-only `twin-auditor` agent lists every twin, list,
  copy, fixture and pin a diff must also touch.
- **Each subproject's `ARCHITECTURE.md`** — **normative for that tree.** Before changing a
  surface, read its `ARCHITECTURE.md` and keep the change inside its layers, placement
  table and rules. Each is an independent document of that surface's design, in the same
  eight sections — Layers, Where things go, Entities, Patterns, Design, Concurrency, Rules,
  Tests — and
  stays current with the tree.
- **Each subproject's `README.md`** — the user-facing guide only: what it is, build, run,
  test, configure, use. No architecture, internals or feature inventories.
- **`usecases/docs/<app>/USECASES.md`** — the illustrated walkthrough of an app: scenarios and
  steps with screenshots/GIFs under `usecases/docs/<app>/img/`, generated by the scripts in
  `usecases/capture-runner/` (never hand-edited). User-facing like a README: no architecture, no
  inventories, no counts. Docs-only: touching `usecases/` runs no CI job.
- **`tools/README.md`** — `moveCheck.mjs` and `commentOnlyDiff.mjs` (move proofs),
  `docPaths.mjs` and `commentPaths.mjs` (every path a doc or a code comment names exists),
  `syncTwins.mjs` (re-copy the twins) and `caps.mjs` (the 230-line, 12-file and comment-share caps).
