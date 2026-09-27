---
name: split-move
description: >-
  The procedure and the proofs for a pure-move refactor in Stencil — splitting a file under
  the 230-line cap, splitting a folder past 12 files, moving or renaming files and folders,
  renaming C++ members — and the traps each one has hit here (import forms, byte-pinned
  copies, cross-surface parity tables, silent C++ shadowing, stale doc paths). Use before
  any split, folder move, rename or decomposition, and when asked to prove a refactor
  changed nothing.
---

# Split, move or rename — and prove it

The shape of a split is in `.claude/rules/architecture.md` (by feature, the shared prefix
names the folder and leaves the file names, ports keep their names, tests mirror the source).
This is how to do one without breaking a tree you did not touch.

## Before moving anything

- **Record the pins** (`.claude/rules/tests.md`) and run the owning suite, so a red after the
  move is yours.
- **Key the rename map on `(surface, basename)`.** `opPlan.js`, `chatController.js`,
  `tipContent.js`, `motionPrefs.js` and dozens more exist on several surfaces. A map keyed on
  the basename rewrites a surface that renamed nothing, and when both names exist there the
  import still resolves — to the wrong module ("does not provide an export named …").
- **List who reads these files by path**, not by import: the parity tables
  (`browser-extension/tests/portParity.test.js`, `vscode-extension/tests/parserParity.test.js`,
  `.claude/tools/twins.json`), cross-surface source readers (`browser/tests/helpers/desktopSource.js`,
  `browser/tests/helpers/extensionCss.js`, `browser-extension/tests/helpers/sources.js`,
  `desktop/tests/support/tip/MainWindow.tooltips.gui.cpp` reading the browser toolbar), CMake
  source lists, and any test that slices its own path. The `twin-auditor` agent lists them
  for a diff.

## JS: every import form

Resolve each specifier against the file's **original** directory, then re-derive it relative
to the new one. Rewriting only the references *to* moved files fixes half the graph: a moved
file's own `'../utils.js'` gains a level and its `'./sibling.js'` loses one.

A quoted-specifier regex misses these, and each fails far from the edit:

- a cache-busting query: `import('../js/ui/chat/view.js?render-once')` fails an existence check;
- a template literal: ``import(`../../js/ui/x.js?rowmenu${tag}`)`` — grep ``import(` ``;
- a specifier without `./`: `host.require('lib/cliLocator.js')`, `@embedFile("../help.txt")`,
  `chrome.scripting.executeScript({ files })`, `join(__dirname, '..', '..')` walks;
- a `.d.ts`-only module: `'./geometry.js'` may resolve to `geometry.d.ts` alone;
- a trailing slash: `path.normpath`/`relpath` strip it, and `dir + name` then glues the
  folder onto the file name — diff every rewritten file and restore specifiers that ended in `/`;
- path maps that are data: the parity tables above move in the same change.

The filesystem is case-insensitive and git is not: match the real directory listing, never
`exists()`. After the move, `grep -aoE "Cannot find module '[^']+'" | sort -u` over the suite's
output names every stale specifier at once.

## Byte-pinned copies move with their originals

A pinned copy imports exactly what its original imports, so it must sit at the **same depth**
below its root (`browser/js/core/script/` ↔ `vscode-extension/src/parser/script/`, both two
levels down to `config/colorNames.json`). Moving an original moves its copy, its row in
`.claude/tools/twins.json` and its row in the parity test, together; then
`node .claude/tools/syncTwins.mjs` re-copies. Never normalize an import away in the parity test —
that is what the pin exists to stop.

A file a port imports keeps its name through a split: `portParity.test.js` reduces a specifier
to its basename so either tree's layout is allowed, which only works while both spell it the
same. `ui/motion/motionPrefs.js` stays as it is for that reason — `lib/rectTween.js` imports it
on both sides.

## Tests follow the split

A test sits in the folder named for the source it covers; a case that spans modules or guards
the whole tree stays at `tests/` root — it has no one home, and Cargo only sees an integration
test directly in `tests/`. A mirrored test folder may sit above the 12-file cap, because a
module commonly carries several test files.

## Desktop and core (C++)

- Includes are bare (`#include "fileStore.hpp"`) and resolve through the include path, so a new
  subfolder joins `STENCIL_GUI_DIRS` in `desktop/cmake/sources/dirs.cmake`; only the
  `#include "../x.hpp"` form carries a depth. Every `.cpp` path in `desktop/cmake/sources/`,
  the per-target sources in `desktop/cmake/tests/`, and
  `usecases/capture-runner/desktop/captureStates.cpp` (a deep relative include) follow.
- A scan that reads a folder non-recursively goes red on a pure move:
  `desktop/tests/layerBoundary.headless.cpp`,
  `desktop/tests/app/setup/MainWindow.chromeShortcuts.gui.cpp`, the tooltips suite. Widen it
  with `QDirIterator::Subdirectories`.
- `CORE_INCLUDE_ALLOWANCE` in `layerBoundary.headless.cpp` is rename-only: when a
  core-including file splits, the core-touching half takes over the entry by being renamed
  into it (`git mv`), never by a new entry.
- A field rename is scoped by filename glob: `LinksDialog` and `OpenImageDialog` share private
  member names on purpose.
- In `core/`, a new, renamed or moved `.cpp` edits the three source lists, and a new folder
  the three include-dir lists (`.claude/rules/core-changes.md`).
- **Splitting a headless suite:** `desktop/tests/support/check.hpp` declares `inline int
  failures`, so one binary spans many TUs — `<name><Area>.headless.cpp` sections plus a
  `<name>Parts.hpp` for includes, fixtures and declarations. Keep the section order, end the
  last section before the summary `printf`, hoist helpers two sections share (or return them
  from the section that makes them), and pass fixture locals under their original names so
  bodies stay byte-identical. Proof: the pre-split binary's stdout equals the split one's,
  ephemeral `127.0.0.1:<port>` normalized.

## C++ member renames

- Dropping a trailing underscore silently rebinds a member to a parameter or local of the
  same name, and `-Wall -Wextra` stay quiet: `x = x`, `const QColor ink = ink.isValid() ? …`,
  `double t = (t - delay) / flight`. The oracle is a throwaway build with
  `-Wshadow -Wself-assign -Wself-move -Wshadow-field-in-constructor`. Detectors miss
  constructor initializer lists, lambda parameter lists, one-letter names and `const Type name`
  locals; fixing the declaration line is not enough — qualify every later use with `this->`.
  Re-derive each affected line from the pre-rename original rather than editing renamed text.
- A bare member named like a Qt method (`size`, `show`, `window`, `rect`, `pos`, `cursor`)
  hides it: name what it holds (`markPx`, `showWord`, `hostWindow`, `cropBox`). An accessor over
  its own member takes `get`; afterwards grep for `void get…()` — a getter never returns void.
- Tests on other surfaces pin C++ source text (`browser/tests/ui/drawToggle-icons.test.js`):
  grep every tree for the old spelling.
- After any split, re-run the OTHER surfaces' suites: a symbol that moved out of a file that
  still exists passes every exists-check. Resolve such pins over a family, never one file
  (`browser/tests/helpers/desktopSource.js`, `extensionCss.js`,
  `browser-extension/tests/helpers/sources.js`).

## Paths a file computes from its own location

`__FILE__` slicing, `Path(__file__).parent.parent`, `path.join(HERE, '..', '..')` break the
moment the file moves, and read as missing data, not a path error. In C++ `__FILE__` carries
the `#include` spelling, so it breaks even when the header never moved. Resolve to a landmark
(walk up until `CLAUDE.md` or `browser/js/config` is in sight). Grep the moved files for
`__FILE__`, `__file__`, `import.meta.url` and bare `'..'` before trusting a green run.

## The proofs

- **JS:** `node .claude/tools/moveCheck.mjs <ref> <paths…>` — the signal is `LOST 0`, with only the
  enclosing wrapper as NEW (a method→function extraction reports its wrapper). It is JS-only:
  handed `.py` or `.cpp` it prints `0 files … LOST 0 NEW 0`, a false pass.
- **Comments:** `node .claude/tools/commentOnlyDiff.mjs <ref> <paths…>`, every file `OK`. A regex
  holding `\/\/` inside a template literal's `${…}` gives a false `CHANGED` there; fall back to
  `git diff -U0` and check every changed line is a `//` line.
- **C++:** never `gcc -fpreprocessed` — `gcc` is the clang shim here and emits empty files,
  which compare equal. Use `normalizeLines` from `.claude/tools/commentOnlyDiff.mjs` over the old file
  and the new pair as multisets, assert the stripped text is **non-empty**, and expect `LOST 0`
  with NEW only scaffolding (`#include`, `namespace {`, braces).
- **Python:** hash `ast` function bodies before and after; for a UI move, diff the set of
  user-facing string literals.
- **Docs and comments:** `node .claude/tools/docPaths.mjs --check` and
  `node .claude/tools/commentPaths.mjs --check` — a move strands paths in `.md` files and in the
  comments that name a twin or a port.
- **Twins:** `node .claude/tools/syncTwins.mjs --check`.
- **Pins identical**, then every suite that **reads** the moved tree, not only the one that
  owns it: parity pins reach across surfaces and go red after the owner reported green.
