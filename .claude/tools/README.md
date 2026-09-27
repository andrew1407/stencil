# Repo tools

Node builtins only — nothing to install, nothing added to any `package.json`.
`node --test .claude/tools/*.test.mjs` runs their suites; CI runs them on every push.

- `moveCheck.mjs`, `commentOnlyDiff.mjs` — prove a refactor was a **pure move**: code changed
  file, not behaviour.
- `docPaths.mjs` — every path a doc names still exists.
- `commentPaths.mjs` — every path a code comment names still exists.
- `syncTwins.mjs` — re-copy every byte-equal port and drift-tested copy from its original.

The two move tools compare a `<gitRef>` against the **working tree** (so an in-progress refactor can be
checked before it is committed), and both take files or directories; a directory expands to
the files git tracks or would track under it — gitignored build output never counts.

## `moveCheck.mjs` — did the code move, or change?

```sh
node .claude/tools/moveCheck.mjs <gitRef> <path...>     # exits 1 if anything is LOST
node .claude/tools/moveCheck.mjs --self-test
```

Extracts every top-level function, arrow-const and class method from the JS on both sides,
strips its comments, collapses its whitespace (never inside a string), and hashes the body
(sha256, first 12 hex). It then diffs the two multisets of hashes:

- **LOST** — a body that existed at `<gitRef>` and exists nowhere now. This is the failure.
- **NEW** — a body that exists now and did not then. Expected when a move also adds code.
- **unchanged** — the proof. A body that only changed file, name or neighbours still matches.

A pure move, even one that splits a file in two, prints `LOST 0   NEW 0`.

```
$ node .claude/tools/moveCheck.mjs HEAD browser/js/core/draw/renderer.js
moveCheck HEAD -> working tree  (1 files then, 1 now)
  unchanged 14   LOST 0   NEW 0
```

## `commentOnlyDiff.mjs` — did a comment sweep touch only comments?

```sh
node .claude/tools/commentOnlyDiff.mjs <gitRef> <path...>   # exits 1 if any file CHANGED
node .claude/tools/commentOnlyDiff.mjs --self-test
```

Strips comments and normalizes whitespace on both sides and compares what is left, exactly.
Every file prints `OK` or `CHANGED`; a changed file also prints the first differing
normalized line, then and now. Languages, by extension:

| | comments stripped |
|---|---|
| `.js` `.mjs` `.cs` `.go` `.rs` `.zig` `.cpp` `.hpp` `.h` `.c` | `//` and `/* */` |
| `.py` | `#` only — a docstring is a statement, and stays |

Anything else is skipped — including `.stc` scripts and the fixture corpus they live in.
A `.stc` is input data, not code: its `#` comments are part of what the lexer is tested on
(a `#ccc` is a colour, not a comment), so stripping them would change the case rather than
prove it unchanged. Diff those files normally.

The scanner is string-aware in both tools (they share it): a `//` inside a string, a template
literal, a regex, a Rust raw string, a C# verbatim string or a Zig `\\` line is content, not a
comment — as the self-tests assert.

## `docPaths.mjs` — does every path a doc names still exist?

```sh
node .claude/tools/docPaths.mjs [--check] [doc.md...]   # --check exits 1 on a dead path
```

Reads every tracked (or would-be-tracked) `.md`: each backticked word shaped like a path and
each relative link. A code path resolves against the repo root, the doc's folder, the doc's
surface root and that root's `js/` and `src/`, or as the tail of any path in the tree
(`src/llm/client.js`, `Application/Llm/…` for the bot's `Stencil.TelegramBot.Application`).
A module stem (`core/parse/formulaParser`), `module.member`, brace alternatives (`{hpp,cpp}`,
each must exist), globs, `<placeholder>` segments, `…/` and Go's `./...` all resolve. A link
resolves relative to its doc only. Fenced blocks are skipped.

Build output (`zig-out/`, `js/wasm/`, `target/`, `build/`, `node_modules/`, `.out/`,
`bot/packages/`, and anything else git ignores) passes. A word that reads as a path but names
something else — an example, a package's contents, a URL route — goes in the `ALLOW` table at
the top of the tool, keyed by its doc. CI runs `--check` on every push, since a move anywhere
can strand a path in any doc.

## `commentPaths.mjs` — does every path a code comment names still exist?

```sh
node .claude/tools/commentPaths.mjs [--check] [file...]   # --check exits 1 on a dead path
```

Reads the comments of every tracked `.js` `.mjs` `.ts` `.cs` `.go` `.rs` `.zig` `.cpp` `.hpp`
`.h` `.c` `.py` and `.css` file — with `commentOnlyDiff`'s string-aware scanners, and block
comments only for CSS, where a `//` is part of a URL. A comment word counts as a path when it
carries a file extension; it resolves by `docPaths`' rules, so a twin in another surface
(`browser/js/…` named from `desktop/`) or its tail (`js/ui/…`, `lib/…`) passes. A `:line` or
`#anchor` tail is dropped, a half `<placeholder>` split by a space is prose, and whatever git
ignores or builds passes. An example or a fixture's own name goes in the tool's `ALLOW` table,
keyed by the file that says it. Vendored code is skipped. CI runs it with the suites.

## `syncTwins.mjs` — re-copy the twins

```sh
node .claude/tools/syncTwins.mjs [--check] [path...]    # --check writes nothing, exits 1 on drift
```

`--hook` is the PostToolUse entry `.claude/settings.json` wires after every Edit/Write: it reads
the call on stdin and, when the edited file is a twin's side and a pair touching it has drifted,
exits 2 with one line — re-copy from the original, or edit the original instead of the copy.

`twins.json` lists every copy another surface ships of a canonical file, by kind:

| kind | what is equal | sync writes |
|---|---|---|
| `copies` | the bytes | the original's bytes |
| `picks` | the named keys of a JSON original | those keys, `JSON.stringify(…, null, 2)` |
| `trees` | every file of a folder, both directions | each file; an orphan copy is deleted |
| `ports` | the body — only the leading `//` header and import specifiers may differ | the copy's header + the original's body, each import respelled the copy's way (by basename) |
| `functions` | each named top-level declaration | the original's declaration in place of the copy's |

A `path` narrows the run to rows whose original or copy lies under it. A port that gains an
import the copy never had is reported `SKIPPED` — respell it by hand once. The surfaces'
parity tests stay the enforcement; `syncTwins.test.mjs` proves the manifest names exactly
the pairs they read (it parses their tables: `portParity`, `dataParity`, `parserParity`,
`test_canonical_drift.py` and the server's embedded-asset tests) and that every pair is in
step. Data a surface embeds inside code — `dataParity`'s accents, icons, page sizes and media
lists — has no file to copy and stays with its own test.

