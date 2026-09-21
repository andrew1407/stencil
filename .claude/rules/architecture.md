---
description: Layer boundaries, the 230-line rule, comment policy, canonical data, UI pins
---

# Architecture rules

The reasoning is in `ARCHITECTURE.md`. These are the parts that fail a build or a review.

## Per-surface `ARCHITECTURE.md` is normative

Every subproject has one (`browser/ARCHITECTURE.md`, `cli/ARCHITECTURE.md`, …). Read it
before editing that tree and keep the change inside its layers, "Where things go" table and
rules. It is an independent document of that surface's design and stays current with the
tree; the `README.md` beside it is user-facing only (build, run, use) and never carries
architecture. `usecases/docs/<app>/USECASES.md` is the illustrated companion: scenarios and
steps only, its images generated under `usecases/docs/<app>/img/` by `usecases/capture-runner/`
(a visual change is followed by a re-run, never by editing a picture), no architecture,
no feature inventory, no counts.

Every one of them carries the same seven `##` sections in this order, each in one fixed
form — a table where every item has the same fields, a list where they are parallel but
uneven, prose where it is one argument:

| Section | Form | Holds |
|---|---|---|
| Layers | prose | the import order left to right, and what lints it |
| Where things go | table | path · holds · rule |
| Entities | `classDiagram` + table | entity · what it is · owned by / lifetime · relates to |
| Patterns | table | pattern · where · notes |
| Design | bold-led bullets | the flows, and any schema this surface owns |
| Rules | numbered list | the invariants |
| Tests | prose | what the suite proves, what it stubs, what it pins |

A new entity, pattern or flow goes into its section, never a new heading. Name what a thing
*is*, never how many there are: counts of suites, files, pinned states or cases go stale on
the next commit and are not design.

## Layers — imports point one way

A layer may use everything to its left, nothing to its right.

- **browser** — `config/` + `utils.js` → `core/` (**no DOM**) → `eventBus/` (`core/emitter.js`) →
  `net/` → `llm/` → console facade (`console/stencilApi.js`) → `ui/` → render.
- **browser-extension** — `lib/` → `config/` → `llm/` → `background/` → `content/` → `popup`,
  `options`, `crop`.
- **vscode-extension** — `src/config/` + `src/parser/` (copied, imports nothing outward) →
  `src/lib/` (`vscode`-free) → `src/*.js` → `src/extension.js`.
- **desktop** — the core seam (`core/` includes the layer lint allows) → controllers → `net/`, `io/` →
  `support/` (motion, theme, widgets, platform) → `canvas/`, `dialogs/`, `llm/` → `app/`.
- **cli** — `core.zig` → `args.zig` → `net.zig` → ops (`pipeline`, `media`) → `llm/` →
  `console/` → `app/` → `main.zig`. **`console/` and `app/` are the only layers allowed to
  write to a terminal**; lower layers return values and errors.
- **server** — `cmd/` → `internal/httpapi` (**transport only** — decode, authorize, encode) →
  service → `store`/`filestore` → `hub` → `protocol`. No business rule in a handler.
- **bot** — `Domain` ← `Application` ← `Infrastructure` ← `Bot`. Dependencies point inward;
  `Domain` stays free of Telegram, HTTP and process types.
- **mcp** — `server/` + tools → `opplan/` → `args` → `pipeline` → `llm`.
- **pystencil** — `_native`/`core` → `image`/`codecs`/`layout` → `editor` → `llm`/`server`/
  `sitesource` → `cli`.

## 230 lines

`maxNewFileLines` is **230** on every surface. A new file over 230 lines fails the ratchet; a
file already listed in the budget may not grow. Shrink it and lower its number in the same
commit. Never raise a number without a note in the budget's `exceptions`.

Budgets: `browser|browser-extension|vscode-extension/tests/sizeBudget.json`,
`core|desktop/tests/sizeBudget.json`, `cli|mcp|pystencil/tests/size_budget.json`,
`server/internal/lint/sizebudget.json`, `bot/tests/Stencil.TelegramBot.Tests/SizeBudget.json`.

## Folders

A folder holds at most **12 direct source files**; a header and its `.cpp`, or a module and its
`.d.ts`, count once. At thirteen the folder splits.

A split is by **feature, never by kind** — `ui/openImage/`, `app/mainWindow/chat/`, not
`helpers/`, `parts/` or `misc/`. Its name is the prefix the files already share, and that prefix
then leaves the file names: `ui/openImage/tabs.js`, not `ui/openImage/openImageTabs.js`. One
class split across translation units keeps the class name on each
(`app/mainWindow/MainWindowChat.cpp`), because every unit defines `MainWindow::`.

A file a byte-pinned port imports keeps its name too: `portParity.test.js` reduces a specifier
to its basename so either tree's layout is allowed, which only works while both spell it the
same. `ui/motion/motionPrefs.js` stays as it is for that reason — `lib/rectTween.js` imports it
on both sides.

Nesting stops three levels below the surface's source root. Tests mirror the split one for one:
a test sits in the folder named for the source it covers, and a case that spans modules or guards
the whole tree stays at `tests/` root. A mirrored test folder may sit **above** the cap, because a
module commonly carries several test files; freeze it in `dirs` rather than splitting it by kind.
A new folder is a new `commentPct` key in that surface's budget, recorded from the lint's own
output and never raised.

`maxFilesPerDir` in every surface's budget enforces the cap, and the folders still above it are
frozen there under `dirs`, exactly like `files`: a frozen number comes down when the folder
splits and never goes up. Only test folders are frozen today — a case that spans modules has no
one home, and Cargo can only see an integration test directly in `tests/`.

## C++ member names

A member is spelled bare — `canvas`, `settings` — with no trailing underscore and no `m_`
prefix. Where a parameter or a local binds the same name, the member use is written
`this->canvas`; a plain `canvas = canvas` there assigns the parameter to itself, and neither
`-Wall` nor `-Wextra` says a word. Where an accessor would collide with its own member, the
accessor takes the `get` prefix (`getCropRect()` over `cropRect`), because the member keeps
the plain name. A member of a QWidget subclass may not take the name of a Qt method it would
hide — `size`, `show`, `window`, `rect` — so it carries what it actually holds (`markPx`,
`showWord`, `hostWindow`, `cropBox`).

`auto_` is the one survivor: its bare form is a keyword.

## Comments

**Two lines maximum, in the body of any file, and only when the code cannot say it**: a
formula, why a constant is that value, a unit, an invariant, a cross-surface coupling. If none
of those is at stake, there is no comment. A block that narrates what the next lines do, or
recounts how a bug was found, is deleted rather than shortened.

**A file's own doc banner is 3–5 lines** — what the file is and its browser/contract twin,
nothing else. One banner per file; a class or function does not get a second one.

**A comment says what a thing MEANS, never where it is wired.** "Every key works both on the
facade and under .settings", "also on host.__stop", "exposed on the app too" — the reader can
see the wiring; it is not content. What earns a line is the meaning: the unit (`ms, clamped
100–3000`), what the empty value stands for (`'' = points follow lineColor`), the range, the
axis, the formula. Delete the rest rather than rewording it, and never write a comment that
refers to itself ("where the comment says so").

The ratchet caps comments as a **share per directory**, so padding one file costs you in
another. Never write: a sprint or phase tag, `(user report)`, "used to", "TODO(name)", or a
sentence that restates the next line.

## Canonical data

**`browser/js/config/` is the only home for a shared data table.** Other surfaces embed it
(qrc alias, `@embedFile`, `include_str!`, `<EmbeddedResource Link>`) or ship a checked-in copy
**with a byte-equality drift test**. A copy without a drift test is a bug. If a value can be
read back out of the core over the C ABI, do that instead of mirroring it.

The same rule covers copied **code**: the `browser/js/ui` + `llm/llmClient` modules in
`browser-extension/src/lib/`, and the modules of `browser/js/core/script/` in
`vscode-extension/src/parser/`, are byte-equal copies pinned in both directions
(`portParity.test.js`, `parserParity.test.js`). Edit the original and re-copy; never fix a
copy in place.

## Typed boundaries

Every public JS module gets a sibling `.d.ts` describing its exported surface; add one when
you create a module or rework its exports.

## UI

- **Pins before motion.** Record the pins before moving UI code; they must be identical after.
- **UI freeze in refactor commits.** A commit that moves code changes no pixel and no string.
  An intended visual change is its own commit, carrying its re-pin and nothing else.
- Prove a move with `node tools/moveCheck.mjs <gitRef> <path…>` and a comment sweep with
  `node tools/commentOnlyDiff.mjs <gitRef> <path…>` (every file `OK`). `LOST 0  NEW 0` holds for
  whole-function and data moves; an extract-class reports one LOST/NEW pair per converted method,
  so the signal is **`LOST 0`, only the wrapper NEW**. For C++, `cppCommentDiff.sh` exits 2 where
  `gcc` is the clang shim and a raw `-fpreprocessed` diff emits EMPTY files — a false pass; use
  `normalizeLines` and check the stripped text is non-empty.
