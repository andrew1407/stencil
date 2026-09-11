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

Nine surfaces plus two cross-surface gates. **Run them one at a time, in this order** —
cheapest first, so a break surfaces in seconds rather than after the 3-minute desktop build.
Never run two native builds concurrently and never pass a bare `-j`: this machine locks up
under a full-width parallel build, which is why the order and the caps below exist.

Report the result of every chunk you ran, including the ones that failed. A chunk you skipped
is not a pass.

## The matrix

| # | Surface | Command (from the repo root) | Expected |
|---|---|---|---|
| 1 | browser | `cd browser && npm test` | 2525 pass, 0 fail (~2 s) |
| 2 | extension | `cd extension && npm test` | 1217 pass, 0 fail (~2 s) |
| 3 | core | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && nice -n 10 cmake --build core/build -j 4 && ctest --test-dir core/build --output-on-failure` | 1/1 |
| 4 | cli | `cd cli && DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer zig build test --summary all` | 378 pass (~6 s) |
| 5 | pystencil | `cd pystencil && python3 -m unittest discover -s tests` | 613 OK |
| 6 | server | `cd server && go test ./...` then `go test -race ./internal/hub/...` | 16 pkgs ok |
| 7 | bot | `cd bot && dotnet test Stencil.TelegramBot.slnx -m:2` | 2117 pass, 0 fail |
| 8 | mcp | `cd mcp && CARGO_BUILD_JOBS=2 cargo test --locked -j 2` | 280 pass / 47 suites, 0 failed |
| 9 | desktop | see below | 50/50 |

The size + comment ratchets and the UI pins are **inside** these suites (`sizeBudget` /
`size_budget` / `SizeBudget` targets, `cssInventory.test.js`, `uiPins` headless, the CLI TUI
goldens), so a green surface already covers its own lint and pins. There is nothing extra to
run for them.

## Desktop (chunk 9) — split it

The GUI target alone is ~192 s; run the rest first so a cheap break is not hidden behind it.

```
cd desktop
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
nice -n 10 cmake --build build -j 4
nice -n 10 ctest --test-dir build -j1 -E mainwindow_gui --output-on-failure   # 49 targets, ~55 s
nice -n 10 ctest --test-dir build -j1 -R mainwindow_gui --output-on-failure   # 1 target,  ~192 s
```

**Known flake:** a handful of GUI cases flip under CPU load. If exactly one GUI case fails,
re-run that target alone on a quiet machine before blaming a change. Two or more failures, or
the same case twice, is real.

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
  cd browser && npm run build-wasm && node --test tests/wasm-parity.test.js
  ```
  17 pass. If `core/` did not change, running the parity test against the existing
  `browser/js/wasm/stencilCore.js` is enough.
- **e2e** — `cd e2e && nice -n 10 npm test` → 89 passed / 29 skipped (~2.5 min). The 29 skips
  are the stack specs; `E2E_STACK=1 npm test` runs them under docker compose.

## Before reporting green

- `git status --porcelain` is empty or holds only what you meant to leave.
- Every chunk above actually ran. Name any you skipped and why.
- If a suite's count differs from the table, say so — the counts are the current pins, and a
  drop means tests disappeared.
