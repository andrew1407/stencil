# CI

Three workflows. All of them are in `.github/workflows/`.

## `ci.yml` — push / PR to `main`

A leading **`changes`** job diffs the push (or the PR against its base) and emits one flag per
job. Fourteen jobs then run independently, each gated on the paths it builds *or reads*:
its own tree, plus the cross-tree inputs it compiles, embeds, or pins parity against.

- `*.md` changes never count.
- An edit to `ci.yml` itself, a `workflow_dispatch`, or a push whose old tip is gone runs
  **everything**.
- The diff logic and the exact path filters live in the `changes` job — read it there rather
  than trusting a copy.

| Job | What it does |
|---|---|
| **browser** | `npm test` (Node's runner). wasm-parity self-skips here — no wasm artifact. |
| **single-file** | `npm install` + `npm run build` in `browser/`. The build self-verifies that the emitted `stencil.html` needs no sibling file and embeds no local path. No lockfile is tracked, so it installs with `npm install` — `npm ci` would need one. |
| **extension** | `npm test` over the chrome/DOM-free modules. |
| **core** | CMake + Doctest via `ctest`. |
| **desktop** | Qt build + the headless ctest cases, **including the `stencil_mainwindow_gui` QtTest e2e**. |
| **wasm** | Builds `core/` fresh with Emscripten and runs `wasm-parity.test.js` against it. **This is the job that catches core ↔ JS-fallback divergence.** |
| **cli** | `zig build` + `zig build test`. |
| **pystencil** | `python3 -m unittest discover -s tests`; recompiles `core/` through `build.py`. |
| **mcp** | Builds the CLI first (its gated e2e tests need the binary on `STENCIL_CLI`), then `cargo build` + `cargo test`. |
| **server** | `go build` + `go test -race`, with Postgres and Redis service containers for the gated store/bus integration tests. |
| **e2e** | Node/Playwright. Brings up db + redis + server via docker compose and drives the real browser app, the unpacked extension, and the server binary over their wire protocols. See `e2e/README.md`. |
| **bot** | `dotnet build` + the offline xUnit suite (no token, server, CLI or Redis). |
| **guard-hook** | `node --test` over the PreToolUse guard's own suite (`.claude/hooks/`). |
| **docker-images** | A matrix over only the images whose build context changed. browser/cli/mcp/bot build **from the repo root** (they compile `core/`); server builds from `./server`. Behaviour is covered elsewhere — this guards packaging. |

## `desktop-packages.yml`

Builds desktop packages for macOS, Windows and Linux. Runs on `v*` tags always, and on
pushes/PRs touching the app's inputs: `desktop/`, `core/`, and the qrc-embedded
`browser/js/config/` + `favicon.svg`.

## `pages.yml`

Deploys the browser app to GitHub Pages on push to `main` (and on demand). Builds the wasm
core fresh and serves `browser/` as the site root, with the single-file `stencil.html`
alongside. Inert until Pages is enabled for the repo with source "GitHub Actions".
