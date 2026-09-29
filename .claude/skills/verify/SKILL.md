---
name: verify
description: >-
  Run Stencil's full local verification matrix — every surface's suite, the UI
  pins and the test-count floors — one surface at a time. Use before
  claiming a refactor is green, at a plan phase gate, or when asked to "run
  everything" / "verify" / "check the matrix". Runs sequentially with capped
  parallelism because the whole matrix at once locks this machine up.
allowed-tools:
  - Bash
  - Read
---

# Verify the whole tree

Every surface, then the cross-surface gates. **Run them one at a time, in this order** —
cheapest first, so a break surfaces in seconds rather than after the 3-minute desktop build.
Never run two native builds concurrently and never pass a bare `-j`: this machine locks up
under a full-width parallel build, which is why the order and the caps below exist.

Report the result of every chunk you ran, including the ones that failed. A chunk you skipped
is not a pass.

## The matrix

| # | Surface | Command (from the repo root) | Expected |
|---|---|---|---|
| 0 | harness | `node --test .claude/hooks/guard.test.mjs .claude/hooks/guard/*.test.mjs .claude/tools/*.test.mjs` | 0 fail — this includes the live doc-path check and the twin check |
| 1 | browser | `cd browser && npm test` | 0 fail |
| 2 | browser-extension | `cd browser-extension && npm test` | 0 fail |
| 3 | vscode-extension | `cd vscode-extension && npm test` | 0 fail |
| 4 | core | `cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release && nice -n 10 cmake --build core/build -j 4 && ctest --test-dir core/build --output-on-failure` | 1/1 — the bench cases skipped |
| 5 | cli | `cd cli && ZIG_LIBC="$TMPDIR/zig-libc.txt" zig build test --summary all` | 0 fail |
| 6 | pystencil | `cd pystencil && python3 -m unittest discover -s tests` | OK |
| 7 | server | `cd server && go test ./...` then `go test -race ./internal/hub/...` | all ok |
| 8 | bot | `cd bot && BOT_TEST_CLI=$PWD/../cli/zig-out/bin/stencil dotnet test Stencil.TelegramBot.slnx -m:2` | 0 fail |
| 9 | mcp | `cd mcp && STENCIL_CLI=$PWD/../cli/zig-out/bin/stencil CARGO_BUILD_JOBS=2 cargo test --locked -j 2` | 0 failed, benches ignored |
| 10 | desktop | see below | all targets pass |

The collapse check lives in the suites, not in this table: each surface carries a
**test-count floor** that fails naming both numbers, so a suite that silently stops running
most of itself goes red on its own. Raise a floor only when its suite has grown well past it.

The count floors, the structural lints, the UI pins and the corpus walkers are **inside**
these suites (`testFloor` / `test_floor` / `TestCountFloor` targets, `layerBoundary`, core's
`noThrowLint`, `cssInventory.test.js`, `uiPins` headless, the CLI TUI and effect goldens, and
each surface's walkers over the op-plan, `.stc`, SSRF-host and image-header corpora), so a
green surface already covers its own lint, pins and walkers. There is nothing extra to run
for them.

If cargo fails inside `~/Documents` with `os error 1` (macOS refuses its hardlinks there), put
the target outside: `CARGO_TARGET_DIR=/tmp/stencil-mcp-target`.

**Chunks 8 and 9 drive the real CLI.** `zig build test` does not refresh `cli/zig-out/bin/stencil`,
so run `cd cli && ZIG_LIBC="$TMPDIR/zig-libc.txt" zig build` first. Without `BOT_TEST_CLI` the bot's
CLI parity cases skip; without `STENCIL_CLI` an out-of-tree `CARGO_TARGET_DIR` hides the CLI from
mcp, whose `cli/*` cases then print `skipping` — both runs stay green, only narrower.

**Chunk 3 is also the parser-copy gate.** `vscode-extension/tests/parserParity.test.js` holds
`src/parser/script/` byte-equal to `browser/js/core/script/` in both directions, so a
script-engine edit that was not re-copied goes red there rather than in the browser chunk. It
needs no install — only `npm run package` (the `.vsix`) does, and that is CI's job, not part
of this matrix.

## Desktop (chunk 10) — split it

The GUI e2e is no longer one binary: it is one `stencil_mainwindow_<area>_gui` target per
feature area, built from one `stencil_gui_objs` object library. The area list is the `foreach`
in `desktop/cmake/tests/gui.cmake` — read it there rather than trusting a copy, since a
split adds areas. So `-E mainwindow_gui` no longer excludes anything — the pattern must carry
the area wildcard.

```
cd desktop
rm -rf build/test-state        # a persisted-state trap fails chatBubbleTailRendersFlushNoGap
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
nice -n 10 cmake --build build -j 4
nice -n 10 ctest --test-dir build -j 2 -E 'mainwindow_.*_gui' --output-on-failure  # the headless half
nice -n 10 ctest --test-dir build -j 2 -R 'mainwindow_.*_gui' --output-on-failure  # the GUI areas
```

**Use `-j 2` for ctest.** The GUI e2e is many short binaries, each opening a real MainWindow;
wider runs over-subscribe and throw one spurious GUI failure per run, a different target each
time, each passing alone.

**Known flake:** a handful of GUI cases flip under CPU load — `chatdock` and `chrome` most
often. If exactly one GUI case fails, re-run that target alone on a quiet machine before
blaming a change. Two or more failures, or the same case twice, is real.

**`uiPins` run bare fails wholesale** — it needs `QT_QPA_PLATFORM=offscreen` and
`STENCIL_NO_ANIM=1`, which only ctest sets.

**`ZIG_LIBC` is not optional on this machine.** Both SDKs `xcrun` hands Zig are MacOSX27.0 (the
Command Line Tools symlink and Xcode 27), which Zig 0.16 cannot compile the C++ core against
(`INFINITY` undeclared). The Command Line Tools still ship 26.5, and a libc file naming it works:
`zig libc | sed -E 's#MacOSX[0-9.]*\.sdk#MacOSX26.5.sdk#' > "$TMPDIR/zig-libc.txt"`. `SDKROOT` and `--sysroot` do not reach Zig's libc++ build, and Xcode's `DEVELOPER_DIR`
now stops every `xcrun` shim (git included) at its unaccepted licence. A bare `zig build` can
look fine purely because the C++ compile was cached, so the break only shows up once something
forces a recompile.

## Cross-surface gates

- **cli release targets** — required whenever `cli/src/` changed. The Windows package has no
  console (termios), and a POSIX-only type reached from a shared path breaks only there:
  `cd cli && env -u ZIG_LIBC zig build -Dtarget=x86_64-windows-gnu -Doptimize=ReleaseSmall
  --prefix "$TMPDIR/stencil-win"` must compile, as must `x86_64-linux-musl` (the other
  `cli-packages.yml` targets rarely differ).
- **wasm parity** — required whenever a `core/` file or a `browser/js` pure-logic module
  changed. The node runner never loads wasm on its own, so rebuild first:
  ```
  export EMSDK_PYTHON=/opt/homebrew/bin/python3.14
  export EM_LLVM_ROOT=/opt/homebrew/opt/emscripten/libexec/llvm/bin
  export EM_BINARYEN_ROOT=/opt/homebrew/opt/emscripten/libexec/binaryen
  export PATH=/opt/homebrew/opt/emscripten/libexec/llvm/bin:/opt/homebrew/opt/emscripten/libexec/binaryen/bin:$PATH
  cd browser && npm run build-wasm && node --test tests/wasm/*.test.js
  ```
  Emscripten 6 ignores the `EM_*` variables without a config file, so the `PATH` line is what
  finds clang and binaryen — without it the build prints an error, exits 0 and leaves the old
  `stencilCore.js` in place: check the file's timestamp. Every spec under `tests/wasm/` passes
  with **0 skipped** — a nonzero `skipped` means the module was not built and the gate proved
  nothing. Run the whole directory: `wasm-parity.test.js`
  alone is only a fraction, the rest living in the other specs beside it. If `core/` did not change, running them against the
  existing `browser/js/wasm/stencilCore.js` is enough.
- **e2e** — `cd e2e && nice -n 10 npm test`; the skips are the stack specs, which
  `E2E_STACK=1 npm test` runs under docker compose with none skipped. Kill a stale static server first (`lsof -nP -iTCP:8188 -sTCP:LISTEN -t
  | xargs -r kill`) or the run may serve another tree's `browser/`.
- **server integration** — `internal/store` and `internal/redisbus` self-skip without
  `TEST_DATABASE_URL` / `REDIS_URL`, so chunk 7 green still leaves those tests unrun. The e2e
  stack already provides both (it stays up; teardown needs `E2E_STACK_DOWN=1`), so right after
  a stack run:
  ```
  cd server && TEST_DATABASE_URL='postgres://stencil:stencil@127.0.0.1:5432/stencil?sslmode=disable' \
    REDIS_URL='redis://127.0.0.1:6379' go test -count=1 ./internal/store/... ./internal/redisbus/... ./internal/eventbus/...
  ```
  None skipped. The setup truncates, so it refuses `DATABASE_URL` — never point it at a
  live database.

## Before reporting green

- `git status --porcelain` is empty or holds only what you meant to leave.
- Every chunk above actually ran. Name any you skipped and why.
- Report the counts you saw. They are a sanity signal, not a gate — the floors in the suites
  are the gate. A count far *below* the previous run's with everything still green means the
  run was narrower than you think: check the command, not the code. Never wrap a suite in
  `timeout` — there is none on this Mac, and the trailing `echo` reports a green that never ran.
- Never time a run inside `$(…)` beside a background watcher: the substitution waits for the
  watcher to release stdout. Under zsh, `grep --include=*.x` dies on `no matches found`.
