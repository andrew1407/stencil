---
name: verify
description: >-
  Run Stencil's full local verification matrix — every surface's suite, the UI
  pins and the size/comment ratchets — one surface at a time. Use before
  claiming a refactor is green, at a plan phase gate, or when asked to "run
  everything" / "verify" / "check the matrix". Runs sequentially with capped
  parallelism because the whole matrix at once locks this machine up.
allowed-tools:
  - Bash
  - Read
---

# Verify the whole tree

Ten surfaces plus three cross-surface gates. **Run them one at a time, in this order** —
cheapest first, so a break surfaces in seconds rather than after the 3-minute desktop build.
Never run two native builds concurrently and never pass a bare `-j`: this machine locks up
under a full-width parallel build, which is why the order and the caps below exist.

Report the result of every chunk you ran, including the ones that failed. A chunk you skipped
is not a pass.

## The matrix

| # | Surface | Command (from the repo root) | Expected |
|---|---|---|---|
| 1 | browser | `cd browser && npm test` | 0 fail, ~3.3k tests (~2 s) |
| 2 | browser-extension | `cd browser-extension && npm test` | 0 fail, ~1.6k tests (~2 s) |
| 3 | vscode-extension | `cd vscode-extension && npm test` | 0 fail, ~140 tests (~2 s) |
| 4 | core | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && nice -n 10 cmake --build core/build -j 4 && ctest --test-dir core/build --output-on-failure` | 1/1 — ~250 cases, **12 skipped** (11 bench + 1 budget) |
| 5 | cli | `cd cli && DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer zig build test --summary all` | 0 fail, ~380 tests (~6 s) |
| 6 | pystencil | `cd pystencil && python3 -m unittest discover -s tests` | OK, ~650 tests |
| 7 | server | `cd server && go test ./...` then `go test -race ./internal/hub/...` | 18 pkgs, 16 with tests, all ok |
| 8 | bot | `cd bot && dotnet test Stencil.TelegramBot.slnx -m:2` | 0 fail, ~2.1k tests |
| 9 | mcp | `cd mcp && CARGO_BUILD_JOBS=2 cargo test --locked -j 2` | 0 failed, ~600 tests, 3 benches ignored |
| 10 | desktop | see below | 66/66 |

**Do not treat those magnitudes as pins.** They drift on every commit that adds a test, and a
table nobody updates teaches you to wave real drops through. The collapse check lives in the
suites instead: each surface's size-budget test asserts a **test-count floor** and fails naming
both numbers, so a suite that silently stops running most of itself goes red on its own. Raise
a floor only when its suite has grown well past it.

The size + comment ratchets, the count floors and the UI pins are **inside** these suites
(`sizeBudget` / `size_budget` / `SizeBudget` targets, `cssInventory.test.js`, `uiPins`
headless, the CLI TUI goldens), so a green surface already covers its own lint and pins. There
is nothing extra to run for them.

**Chunk 3 is also the parser-copy gate.** `vscode-extension/tests/parserParity.test.js` holds
`src/parser/script*.js` byte-equal to `browser/js/core/script*.js` in both directions, so a
script-engine edit that was not re-copied goes red there rather than in the browser chunk. It
needs no install — only `npm run package` (the `.vsix`) does, and that is CI's job, not part
of this matrix.

## Desktop (chunk 10) — split it

The GUI e2e is no longer one binary: it is **15 `stencil_mainwindow_<area>_gui` targets**
(canvas, chatCards, chatCompact, chatDock, chatPanel, chatTurns, chrome, composition, menuKeys,
menus, motion, projects, theme, toolbar, tooltips) built from one `stencil_gui_objs` object library.
So `-E mainwindow_gui` no longer excludes anything — the pattern must carry the area wildcard.

```
cd desktop
rm -rf build/test-state        # a persisted-state trap fails chatBubbleTailRendersFlushNoGap
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
nice -n 10 cmake --build build -j 4
nice -n 10 ctest --test-dir build -j1 -E 'mainwindow_.*_gui' --output-on-failure  # 51 targets
nice -n 10 ctest --test-dir build -j1 -R 'mainwindow_.*_gui' --output-on-failure  # 15 targets
```

Whole suite at `-j1` is ~178 s; at **`-j 16` it is ~14 s** and that is safe here (the GUI
targets are short now), so prefer `ctest --test-dir build -j 16` when the machine is quiet.

**Known flake:** a handful of GUI cases flip under CPU load — `chatdock` and `chrome` most
often. If exactly one GUI case fails, re-run that target alone on a quiet machine before
blaming a change. Two or more failures, or the same case twice, is real.

**`uiPins` run bare looks like 12 failures** — it needs `QT_QPA_PLATFORM=offscreen` and
`STENCIL_NO_ANIM=1`, which only ctest sets.

**`DEVELOPER_DIR` is not optional on this machine.** The Command Line Tools SDK symlink points
at MacOSX27.0, which Zig 0.16 cannot compile the C++ core against (`INFINITY` undeclared);
Xcode's 26.5 SDK still works. A bare `zig build` can look fine purely because the C++ compile
was cached, so the break only shows up once something forces a recompile.

## Cross-surface gates

- **wasm parity** — required whenever a `core/` file or a `browser/js` pure-logic module
  changed. The node runner never loads wasm on its own, so rebuild first:
  ```
  export EMSDK_PYTHON=/opt/homebrew/bin/python3.14
  export EM_LLVM_ROOT=/opt/homebrew/opt/emscripten/libexec/llvm/bin
  export EM_BINARYEN_ROOT=/opt/homebrew/opt/emscripten/libexec/binaryen
  cd browser && npm run build-wasm && node --test tests/wasm-parity*.test.js
  ```
  44 pass across the four parity specs, **0 skipped** — a nonzero `skipped` means the module
  was not built and the gate proved nothing. Keep the glob: `wasm-parity.test.js` alone is only
  17 of the 44, the rest living in the `-history`, `-projects` and `-state` specs. If `core/`
  did not change, running the parity test against the existing
  `browser/js/wasm/stencilCore.js` is enough.
- **e2e** — `cd e2e && nice -n 10 npm test` → 91 passed / 29 skipped (**~26 s** since the
  per-project `workers: 4`; stack runs stay `workers: 1`). The 29 skips
  are the stack specs; `E2E_STACK=1 npm test` runs them under docker compose → **120 passed,
  0 skipped (~3 min)**. Kill a stale static server first (`lsof -nP -iTCP:8188 -sTCP:LISTEN -t
  | xargs -r kill`) or the run may serve another tree's `browser/`.
- **server integration** — `internal/store` and `internal/redisbus` self-skip without
  `TEST_DATABASE_URL` / `REDIS_URL`, so chunk 7 green still leaves 21 tests unrun. The e2e
  stack already provides both (it stays up; teardown needs `E2E_STACK_DOWN=1`), so right after
  a stack run:
  ```
  cd server && TEST_DATABASE_URL='postgres://stencil:stencil@127.0.0.1:5432/stencil?sslmode=disable' \
    REDIS_URL='redis://127.0.0.1:6379' go test -count=1 ./internal/store/... ./internal/redisbus/... ./internal/eventbus/...
  ```
  21 pass, 0 skipped. The setup truncates, so it refuses `DATABASE_URL` — never point it at a
  live database.

## Before reporting green

- `git status --porcelain` is empty or holds only what you meant to leave.
- Every chunk above actually ran. Name any you skipped and why.
- Report the counts you saw. They are a sanity signal, not a gate — the floors in the suites
  are the gate. A count far *below* the magnitude above with everything still green means the
  run was narrower than you think: check the command, not the code.
