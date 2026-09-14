# The `.stc` script contract

The normative definition of Stencil's script language: what a `.stc` file means, what every
surface must do with it, and what it must say when the script is wrong. Machine-readable
proof lives in the fixture corpus at `browser/js/config/script/fixtures/`.

---

## §0 Normative artifacts

| Artifact | Role | Pinned by |
|---|---|---|
| `core/script/` | the one parser, expander and lowerer | `core/tests/script*.test.cpp` |
| `browser/js/core/script*.js` | the JS fallback, op-for-op identical | `browser/tests/wasm-parity.test.js` |
| `browser/js/config/script/fixtures/` | the shared corpus every surface replays | each surface's walker |
| `core/cliApi.h` `stencil_cli_script*` | the C ABI the CLI and pystencil drive | `core/tests/scriptApi.test.cpp` |
| `cli/CONTRACT.md` §5 | `--script`, `--script-plan`, `--script-check` | `cli/tests/script_test.zig` |

`core/` parses, validates and lowers. It never opens a file, fetches a URL, decodes an image
or writes one — the adapters do that, driving the op stream this document defines.

---

## §1 Lexical structure

- UTF-8. A statement ends at a newline **or** a `;`, which are interchangeable.
- **`#` starts a comment** that runs to the end of the line — unless the token it opens is a
  valid hex colour (`#ccc`, `#ccce`, `#cccccc`, `#ccccccee`). So `@filter #ccc # tint` is a
  colour followed by a comment.
- **Directives** are `@` + letters, digits and `-`. Directive names and keywords are
  **case-insensitive** (`@CROP` ≡ `@crop`, `BW` ≡ `bw`). **Paths, URLs and template names are
  case-sensitive.**
- **Strings** are double-quoted, with `\"` and `\\` as the only escapes.
- **Numbers** are `-?\d+(\.\d+)?` with an optional directly attached unit `px`, `cm`, `mm`,
  `in` or `%`. A bare number takes the current `@use` unit.
- **`://` never breaks a word and never opens a block**, so a URL survives lexing intact.
- **Points** are `(x, y)`. A unit may attach to a component (`(14px, 50%)`) or to the pair
  after its `)` (`(56, 90)cm`); a pair unit fills in only for components without their own.
  Commas between points are optional.

## §2 Blocks

`@source <spec>:` and `@stencil <name>:` open a block. A block body is either:

- **indented** past its header — it then ends at the first statement back at or left of the
  header's column; or
- **not indented** — it then runs to the next block header.

There is no `end`. A block header always closes the block above it.

Statements before any `@source` form the **project block**: they apply to whatever is already
open (the editor's current project, or the CLI's `-i` input).

`@source` specs are classified for the adapter that opens them: `http(s)://…` is a **url**, a
spec containing `*`, `?` or `[` is a **glob**, one ending in `/` is a **dir**, anything else is
a **file**. Resolving them — fetching, listing, globbing — is the adapter's job.

## §3 Directives

| Directive | Meaning |
|---|---|
| `@crop x1=… x2=… y1=… y2=… [aspect=W:H]` | crop by named edges |
| `@crop <a>` / `<a> <b>` / `<x1> <y1> <x2> <y2>` | crop by insets (below) |
| `@filter bw\|sepia\|invert\|contour\|none\|<colour>` | a filter mode, or a colour for a duotone |
| `@use <unit>` | the default unit for bare numbers |
| `@use line <groups>` | the style for every shape after it |
| `@use stencil <name> [args…]` | expand a template here |
| `@line (x,y) (x,y) …` | a polyline through two or more points |
| `@rect (x,y) (x,y) …` | a closed shape; exactly two points are opposite corners |
| `@layout <path\|url> [combine\|replace]` | draw a layout JSON, combining by default |
| `@frame <n>` | pick a video frame, and start a fresh set of edits |
| `@save [target]` | write the result (below) |
| `@undo [selectors…]` / `@redo [n]` | see §6 |

**Crop insets.** One value insets all four sides; two inset x then y; four are read
`x1 y1 x2 y2`. A leading `-` on a united value measures from the far edge, so an inset of
`10%` becomes `x1=10% x2=-10%`. Mixing the key form and the positional form is an error.

**`@save` targets.** Bare, the CLI writes `<source-dir>/<base>-stencil.<ext>`; a directory
gets `<dir>/<base>-stencil.<ext>`; a bare name gets `<name>.<ext>` with the extension inferred
from the source; a full path is taken verbatim. A video frame's base is `<base>-frame-<n>`.
On the editors a bare `@save` saves the open project and `@save <name>` renames it first.

## §4 Units and lengths

`px`, `cm`, `mm`, `in` and `%` are accepted everywhere a length is. A bare number means the
current `@use` unit, which starts at `px` and lasts to the end of its block. Lowering makes
every length explicit, so `23` under `@use %` becomes `23%`.

A leading `-` on a **united** length means *measured from the far end of that axis*, not a
negative length: on a 200px-wide image, `-10%` is x = 180.

Lengths are resolved **lazily, against the live image**, because a crop changes the size
mid-script. The host passes the dimensions it holds when it reaches the op.

## §5 Colours and line style

A colour is a hex literal (`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`), a CSS colour name, or
`transparent` — the same vocabulary the rest of Stencil accepts.

`@use line` takes comma-separated groups **in any order**:

| Group | Sets |
|---|---|
| a bare colour | the stroke colour (a second one is an error) |
| `solid` / `dashed` / `dotted` | the stroke style |
| a bare number, or `<n>px` | the stroke thickness |
| `fill <colour>` | the fill of closed shapes |
| `point <colour> <size>` | the point marker (both parts optional) |
| a bare unit | the default unit, as `@use <unit>` |

A `@rect` is a `@line` with `locked` set, which is what closes it and enables the fill.

## §6 Templates

`@stencil <name>:` defines one. The name is every word before the `:`, so `lines and rect` is
a single name; quote it to be explicit. Parameters are positional: `@1`, `@2`, … and the
highest one referenced **is** the template's arity.

`@use stencil <words…>` resolves the **longest defined name that prefixes the words**; whatever
remains is the argument list. So with `style` and `style bold` both defined,
`@use stencil style bold` calls the second one with no arguments.

Passing the wrong number of arguments is an error, as is naming a template that does not
exist. A template no `@use stencil` reaches is a warning. Nesting is allowed and capped at
`MAX_TEMPLATE_DEPTH`; a template that reaches itself hits that cap and is reported.

## §7 Undo and redo

Edits (`@crop`, `@filter`, `@line`, `@rect`, `@layout`) are numbered `1..n` **as written**,
templates included, and are never renumbered. `@frame` starts a fresh set.

| Form | Undoes |
|---|---|
| `@undo` | the last edit still live |
| `@undo N` | edit N |
| `@undo -N` | the N-th edit from the end |
| `@undo 1 2 -1` | each named edit |
| `@undo @line (1,1) (2,2)` | the last edit whose text matches |
| `@redo [n]` | brings back the most recently undone edit, n times |

**Undo is resolved when the script is lowered, not when it runs.** At every `@save` — and at
the end of each block — the lowerer emits one `undo` of however many steps it takes to reach
the last point where the applied and surviving edits agree, then replays the survivors. An
adapter therefore only ever executes ordinary ops; it never computes an undo count. A replay
that would carry the stream to `MAX_OPS` is refused with `E_LIMIT_OPS` instead (§9), so a
long `@undo`/`@redo` cycle cannot multiply the op stream.

## §8 Diagnostics

Every diagnostic carries a stable code, which is what the fixtures and the editors key off.
**A script with any error executes nothing at all.** Warnings execute.

| Code | Severity | Raised when |
|---|---|---|
| `E_UNKNOWN_DIRECTIVE` | error | no such directive; names the nearest within two edits |
| `E_BAD_TOKEN` | error | a token is not what the directive expects there |
| `E_UNTERMINATED_STRING` | error | a quote runs to the end of the line |
| `E_MISSING_COLON` | error | `@source` or `@stencil` without its `:` |
| `E_ARG_COUNT` | error | a directive is missing a required argument |
| `E_DUPLICATE_TEMPLATE` | error | two templates share a name |
| `E_UNDEFINED_TEMPLATE` | error | `@use stencil` names nothing; suggests a near match |
| `E_TEMPLATE_ARITY` | error | argument count ≠ arity |
| `E_TEMPLATE_RECURSION` | error | templates nest past `MAX_TEMPLATE_DEPTH` |
| `E_TEMPLATE_PARAM_INDEX` | error | `@n` outside the template's arity |
| `E_CROP_MIXED_FORM` | error | key form and insets in one `@crop` |
| `E_CROP_ARITY` | error | an inset list that is not 1, 2 or 4 values |
| `E_CROP_UNKNOWN_KEY` | error | a crop key outside `x1 x2 y1 y2 aspect` |
| `E_UNKNOWN_FILTER` | error | `@filter` names neither a mode nor a colour |
| `E_DUP_LINE_COLOR` | error | two stroke colours in one `@use line` |
| `E_LINE_NEEDS_POINTS` | error | a shape with fewer than two points |
| `E_FRAME_OUTSIDE_SOURCE` | error | `@frame` in the project block |
| `E_DUPLICATE_FRAME` | error | the same frame twice in one block |
| `E_UNDO_NO_EDITS` | error | `@undo` where the block has no edits |
| `E_UNDO_OUT_OF_RANGE` | error | a selector naming no edit |
| `E_LIMIT_*` | error | a cap in §9 exceeded |
| `W_UNUSED_TEMPLATE` | warning | a template nothing uses |
| `W_EMPTY_BLOCK` | warning | a `@source` block that does nothing |
| `W_NOTHING_TO_REDO` | warning | `@redo` with nothing undone |
| `W_AMBIGUOUS_UNDO` | warning | `@undo <text>` matched several edits |
| `W_TRAILING_TOKENS` | warning | tokens after a block's `:` |

A diagnostic carries a 1-based line and column and a length, so an editor can underline
exactly the offending span. Diagnostics are reported in source order.

The check line format, which the editors parse, is:

```
<file>:<line>:<col>: error|warning: <message> [<CODE>]
```

## §9 Caps

Identical in C++ and in the JS port, so both reject the same input, with the same code:

| Cap | Value | Reports |
|---|---|---|
| `MAX_LINES` | 20000 | `E_LIMIT_LINES` |
| `MAX_TOKENS` | 200000 | `E_LIMIT_TOKENS` |
| `MAX_OPS` | 5000 | `E_LIMIT_OPS` |
| `MAX_BLOCKS` | 256 | `E_LIMIT_BLOCKS` |
| `MAX_TEMPLATES` | 256 | `E_LIMIT_TEMPLATES` |
| `MAX_TEMPLATE_DEPTH` | 16 | `E_TEMPLATE_RECURSION` |
| `MAX_POINTS_PER_LINE` | 200 | `E_LIMIT_POINTS` |
| `MAX_SOURCE_CHARS` | 1024 | `E_LIMIT_SOURCE` |

A cap is never silent: reaching `MAX_TOKENS` stops the lexer and says so, rather than dropping
the rest of the file. `MAX_OPS` bounds three places, each reporting `E_LIMIT_OPS` once — the
edits a script writes, the statements a nested `@use` fans out before any op exists, and the
replay §7 emits; a refused replay also stops the lowering there, so its block yields nothing.

## §10 Per-surface execution

| Surface | Entry | `@source` may name | `@save` writes | Undo |
|---|---|---|---|---|
| cli, one-shot | `--script <file>` | file, url, dir, glob, video | a file, `-stencil` suffixed | a rewind and replay — there is no session to step, so the runner re-opens the input and re-applies the surviving edits |
| cli console | `/script`, `/script-run` | nothing: the console owns a session, not files, so a `@source` block is reported and skipped and the ops apply to the loaded image | nothing: a `@save` does not write here, only `/save` does | the console session's history |
| pystencil, batch | `run_script` | file, url, dir, glob — **no video**: the package is stdlib-only and carries no decoder, so `@frame` is reported | as the cli one-shot | the editor history |
| pystencil, editor | `Editor.script` | nothing: the editor already holds the image the block header names, so `@open` no-ops | as the cli one-shot | the editor history |
| browser | the script window, the context menu's editor, a dropped `.stc`, `stencil.execScript(text)` | **url only** | the project | the project history |
| desktop | the script dialog, a dropped `.stc` | url or local path | the project | the script's own checkpoints, one per §7-numbered edit — the project's line history does not cover a crop or a filter |
| bot | `/script`, a `.stc` upload | url only | the active **server project**, through the op-plan `save` action; the rendered reply follows any mutation | the session history |
| mcp | `stencil_script` | file or url — a read is not fenced | inside the sandbox root: `--confine-output` refuses a `@save` that climbs out | not applicable |

A surface that cannot honour a directive reports it rather than skipping it silently.

**Security.** A `@source` URL is fetched through the surface's one fetch guard, as
user-named input. A script is first-party: it is what the user typed, uploaded or dropped, so
it may name local paths on the surfaces above that allow them — but an adapter forwarding a
model-chosen script still passes `--confine-output`.

## §11 Fixtures

`browser/js/config/script/fixtures/` holds the corpus; `_schema.md` beside it describes the
file triple and the `err-*` naming rule. The four `tour-*` cases are the language's worked
examples and are what the READMEs point at. Adding a directive means adding a fixture.
