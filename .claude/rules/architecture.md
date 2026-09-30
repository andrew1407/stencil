---
description: Layer boundaries, the 230-line rule, folders, C++ member names, comment policy, canonical data, UI pins
---

# Architecture rules

The reasoning is in `ARCHITECTURE.md`; each surface's own `ARCHITECTURE.md` is normative for its
tree (how those docs are written is `docs.md`, which loads with them). These are the parts that
fail a build or a review.

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

A file stays under **230 lines** on every surface. A new file is written under it; a file
that has grown past it is split by feature when it is next touched, not padded further. No
source, test or stylesheet file is over it today. Nothing enforces this — the review does.

## Folders

A folder holds at most **12 direct source files**; a header and its `.cpp`, or a module and its
`.d.ts`, count once. At thirteen the folder splits.

A split is by **feature, never by kind** — `ui/openImage/`, `app/chat/`, not
`helpers/`, `parts/` or `misc/`. Its name is the prefix the files already share, and that prefix
then leaves the file names: `ui/openImage/tabs.js`, not `ui/openImage/openImageTabs.js`. One
class split across translation units keeps the class name on each
(`app/chat/MainWindowChatDock.cpp`), because every unit defines `MainWindow::`.

A file a byte-pinned port imports keeps its name. Nesting stops three levels below the
surface's source root. Tests mirror the split one for one, and a case that spans modules stays
at `tests/` root; a mirrored test folder is the one place the cap does not apply, and is never
split by kind to get under it. The procedure and its traps are the `split-move` skill.

## C++ member names

A member is spelled bare — `canvas`, no trailing underscore, no `m_`. Where a parameter or a
local binds the same name, the member use is `this->canvas`: a plain `canvas = canvas` assigns
the parameter to itself, silently. An accessor that would collide with its member takes `get`
(`getCropRect()`), and a QWidget member never takes the name of a Qt method it would hide
(`size`, `show`, `window`, `rect`). `auto_` is the one survivor: its bare form is a keyword.

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

A directory's comment share stays under **a fifth of its lines**; the doc banners alone reach
that in a folder of small files, so a body comment there has to earn its place. Never write: a
sprint or phase tag, "used to", "TODO(name)", or a sentence that restates the next line. A
`(user report)` tag is the exception, kept on purpose: it records that a constant or rule answers
something a user saw, which the code cannot say — leave the ones in the tree alone.

## Canonical data

**`common/` is the only home for anything two or more surfaces share** — tables in
`common/config/`, corpora in `common/fixtures/`, brand art in `common/icons/`, samples in
`common/samples/`. Other surfaces serve, embed or read it (the browser's static servers, qrc
alias, `@embedFile`, `include_str!`, `<EmbeddedResource Link>`), generate their copy at build
time (pystencil, server), or — where a package cannot reach outside its folder (the Chrome
extension, the `.vsix`) — ship a checked-in copy **with a byte-equality drift test**. A copy
without a drift test is a bug. If a value can be read back out of the core over the C ABI, do
that instead of mirroring it. A table only the browser reads stays in `browser/js/config/`.

The same rule covers copied **code** — the ports in `browser-extension/src/lib/` and the parser
copy in `vscode-extension/src/parser/`, pinned both ways by `portParity.test.js` and
`parserParity.test.js` (the parser copy byte-equal except the import specifiers its
`twins.json` entry's `rewrite` declares). A copy is never fixed in place, and a new one gets its row in
`tools/twins.json` and in the parity test that pins it, together.

## Typed boundaries

Every public JS module gets a sibling `.d.ts` describing its exported surface; add one when
you create a module or rework its exports.

## UI

- **Pins before motion.** Record the pins before moving UI code; they must be identical after.
- **UI freeze in refactors.** Moving code changes no pixel and no string. An intended visual
  change is re-pinned on purpose and named as such, never absorbed into a move's proof.
- **Prove it.** A split, a move or a rename is proved with the `split-move` skill's proofs
  (`moveCheck.mjs`: `LOST 0`, only the wrapper NEW; `commentOnlyDiff.mjs`: every file `OK`).
