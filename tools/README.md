# Repo tools

Checks that keep Stencil's surfaces, docs and copied files consistent. Each is one Node script
run from the repo root, using Node builtins only: nothing to install, nothing added to any
`package.json`.

## Which tool do I need?

| You are about to… | Run | It fails when |
|---|---|---|
| finish a split, move or rename of JS code | `node tools/moveCheck.mjs <gitRef> <path...>` | a function body that existed at `<gitRef>` exists nowhere now |
| finish a comment-only sweep | `node tools/commentOnlyDiff.mjs <gitRef> <path...>` | anything other than comments or whitespace changed |
| move, rename or delete any file | `node tools/docPaths.mjs --check` and `node tools/commentPaths.mjs --check` | a doc or a code comment names a path that no longer exists |
| edit a file another surface ships a copy of | `node tools/syncTwins.mjs` (then `--check`) | a copy differs from its original |

Every tool prints a one-line summary last (`docPaths: 0 dead paths`), so the last line says
whether it passed.

| Exit code | Meaning |
|---|---|
| `0` | passed (or, without `--check`, reported or re-synced) |
| `1` | failed: something LOST, CHANGED, dead or drifted |
| `2` | bad arguments; from the `syncTwins` hook, a drift note for the editor |

## Common rules

- Run from anywhere inside the checkout; paths are resolved against the repo root.
- `<gitRef>` is any commit-ish (`HEAD`, `main`, a SHA). The move tools compare it against the
  **working tree**, so an uncommitted refactor can be checked before it is committed.
- A `<path>` may be a file or a directory. A directory expands to the files git tracks, or
  would track, under it; anything gitignored (build output) never counts.

## `moveCheck.mjs` — did the code move, or change?

```sh
node tools/moveCheck.mjs <gitRef> <path...>
node tools/moveCheck.mjs --self-test
```

**Checks:** every top-level function, arrow-const and class method in the JS on both sides.
Each body has its comments stripped and its whitespace collapsed (never inside a string), and is
hashed. The two sets of hashes are compared.

**Output:**

```
$ node tools/moveCheck.mjs HEAD browser/js/core/draw/renderer.js
moveCheck HEAD -> working tree  (1 files then, 1 now)
  unchanged 14   LOST 0   NEW 0
```

| Word | Meaning |
|---|---|
| `unchanged` | bodies found on both sides; a body that only changed file, name or neighbours still matches |
| `LOST` | a body at `<gitRef>` found nowhere now: **the failure** — behaviour changed |
| `NEW` | a body that is new; expected only for the wrapper a split adds |

A pure move, even one that splits a file in two, prints `LOST 0`.

**On failure:** each `LOST` line reads `<hash>  <file>:<line> <name>` as of `<gitRef>`. Restore it
exactly, or, if the change was intended, it is not a pure move: land it separately.

## `commentOnlyDiff.mjs` — did a comment sweep touch only comments?

```sh
node tools/commentOnlyDiff.mjs <gitRef> <path...>
node tools/commentOnlyDiff.mjs --self-test
```

**Checks:** strips comments and normalizes whitespace on both sides, then compares what is left
exactly. Each file prints `OK` or `CHANGED`; a changed file also prints its first differing
normalized line, then and now.

| Extensions | Comments stripped |
|---|---|
| `.js` `.mjs` `.cs` `.go` `.rs` `.zig` `.cpp` `.hpp` `.h` `.c` | `//` and `/* */` |
| `.py` | `#` only; a docstring is a statement and stays |

Other files are skipped, including `.stc` scripts and their fixture corpus: a `.stc` `#` can be
content (`#ccc` is a colour), so diff those normally.

**On failure:** the printed line is the code that changed; revert it, or land it as its own change.

## `docPaths.mjs` — does every path a doc names still exist?

```sh
node tools/docPaths.mjs [--check] [doc.md...]
```

**Checks:** every tracked `.md` (or only the ones named): each backticked word shaped like a path,
and each relative link. Fenced code blocks are skipped. Without `--check` it only reports.

**Output:** one line per dead path, `<doc>:<line>  path|link  <token>`, then the summary.

**How a path resolves.** A backticked path passes if it exists relative to any of:

- the repo root, the doc's own folder, the doc's surface root, or that root's `js/` or `src/`;
- the tail of any path in the tree, so `ui/modal/shell.js` matches `browser/js/ui/modal/shell.js`
  (and `Application/Llm/…` matches the bot's `Stencil.TelegramBot.Application` project).

A module stem (`core/parse/formulaParser`), `module.member`, brace alternatives (`{hpp,cpp}`,
each must exist), globs, `<placeholder>` segments, `…/` and Go's `./...` all resolve. A link
resolves relative to its doc only. Build output (`zig-out/`, `build/`, `target/`,
`node_modules/`, `js/wasm/`, and whatever git ignores) always passes.

**On failure:** fix the doc to name the path as it is now. If the word only looks like a path (an
example, a package's contents, a URL route), add it to the `ALLOW` table at the top of
`docPaths.mjs`, keyed by the doc that says it.

## `commentPaths.mjs` — does every path a code comment names still exist?

```sh
node tools/commentPaths.mjs [--check] [file...]
```

**Checks:** the comments of every tracked `.js` `.mjs` `.ts` `.cs` `.go` `.rs` `.zig` `.cpp` `.hpp`
`.h` `.c` `.py` and `.css` file (block comments only in CSS, where `//` is part of a URL). A comment
word counts as a path when it carries a file extension, and resolves by `docPaths`' rules, so a
twin in another surface (`browser/js/…` named from `desktop/`) or a tail (`js/ui/…`) passes. A
`:line` or `#anchor` suffix is dropped. Vendored code is skipped.

**Output:** one line per dead path, `<file>:<line>  <token>`, then the summary.

**On failure:** fix the comment. An example or a fixture's own name goes in the tool's `ALLOW`
table, keyed by the file that says it.

## `syncTwins.mjs` — keep every copy equal to its original

```sh
node tools/syncTwins.mjs [path...]           # re-copy every drifted pair (writes files)
node tools/syncTwins.mjs --check [path...]   # report only; exits 1 on drift
```

Some packages cannot read outside their folder (the Chrome extension, the `.vsix`), so they
ship copies of canonical files. **Never edit a copy:** edit the original, then run this tool.
A `path` narrows the run to pairs whose original or copy lies under it.

**Output:** `--check` prints `DRIFT <pair>  (from <original>; pinned by <test>)` and the
differences; a sync prints what it rewrote. Both end with the summary.

**The manifest, `twins.json`,** lists every pair by kind. `from` is always the original; paths
are repo-relative; `generated` rows are written by a build step and checked only when present.

| Kind | What must be equal | What a sync writes |
|---|---|---|
| `copies` | the bytes | the original's bytes |
| `picks` | the named keys of a JSON original | those keys, as `JSON.stringify(…, null, 2)` |
| `trees` | every file of a folder, both ways | each file; a copy with no original is deleted |
| `ports` | the body; only the leading `//` header and import specifiers may differ | the copy's header over the original's body, imports respelled the copy's way |
| `functions` | each named top-level declaration | the original's declaration in place of the copy's |

A pair the tool cannot rewrite safely is reported `SKIPPED … — edit by hand`: a port whose
original gained an import the copy never had (respell it in the copy once), or a `functions`
row whose declaration is missing on one side.

**Adding a copy:** add its row to `twins.json` and to the parity test that pins it, together.
The surfaces' parity tests stay the enforcement; `syncTwins.test.mjs` proves the manifest lists
exactly the pairs those tests read.

**Editor hook:** `.claude/settings.json` runs `node tools/syncTwins.mjs --hook` after every
Edit/Write. It reads the call on stdin and, when the edited file belongs to a drifted pair,
exits `2` with one line: re-copy from the original, or edit the original instead of the copy.

## Tests

```sh
node --test tools/*.test.mjs
```

CI runs these on every push, docs-only included: a move anywhere can strand a path in any doc,
and an edit to any original can drift a copy. The scanners are string-aware (a `//` inside a
string, template literal, regex, Rust raw string, C# verbatim string or Zig `\\` line is content,
not a comment), which the `--self-test` runs assert.
