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
architecture.

All ten carry the same seven `##` sections in this order, each in one fixed form — a table
where every item has the same fields, a list where they are parallel but uneven, prose where
it is one argument:

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

- **browser** — `config/` + `utils.js` → `core/` (**no DOM**) → bus (`core/emitter.js`) →
  `net/` → `llm/` → console facade (`console/stencilApi.js`) → `ui/` → render.
- **browser-extension** — `lib/` → `config/` → `llm/` → `background/` → `content/` → `popup`,
  `options`, `crop`.
- **desktop** — the core seam (`core/` includes the layer lint allows) → controllers → `net/`, `io/` →
  `support/` (motion, theme, widgets, platform) → `canvas/`, `dialogs/`, `llm/` → `app/`.
- **cli** — `core.zig` → `args.zig` → `net.zig` → ops (`pipeline`, `image`, `layout`, `page`,
  `video`) → `llm/` → `console/` → `main.zig`. **`console/` is the only layer allowed to write
  to a terminal**; lower layers return values and errors.
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

Budgets: `browser|browser-extension/tests/sizeBudget.json`, `core|desktop/tests/sizeBudget.json`,
`cli|mcp|pystencil/tests/size_budget.json`, `server/internal/lint/sizebudget.json`,
`bot/tests/Stencil.TelegramBot.Tests/SizeBudget.json`.

## Comments

At most ~3 lines, and only what the code cannot say: an invariant, a unit, a cross-surface
coupling, why a constant is that value. The ratchet caps comments as a **share per
directory**, so padding one file costs you in another.

Never write: a sprint or phase tag, `(user report)`, "used to", "TODO(name)", or a sentence
that restates the next line.

## Canonical data

**`browser/js/config/` is the only home for a shared data table.** Other surfaces embed it
(qrc alias, `@embedFile`, `include_str!`, `<EmbeddedResource Link>`) or ship a checked-in copy
**with a byte-equality drift test**. A copy without a drift test is a bug. If a value can be
read back out of the core over the C ABI, do that instead of mirroring it.

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
