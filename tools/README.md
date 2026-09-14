# Move-verification tools

Refactors here are meant to be **pure moves**: code changes file, not behaviour. These tools
prove it mechanically, against a git ref, before and after. Node builtins only — nothing to
install, nothing added to any `package.json`.

Both compare a `<gitRef>` against the **working tree** (so an in-progress refactor can be
checked before it is committed), and both take files or directories; a directory expands to
the files git tracks or would track under it — gitignored build output never counts.

## `moveCheck.mjs` — did the code move, or change?

```sh
node tools/moveCheck.mjs <gitRef> <path...>     # exits 1 if anything is LOST
node tools/moveCheck.mjs --self-test
```

Extracts every top-level function, arrow-const and class method from the JS on both sides,
strips its comments, collapses its whitespace (never inside a string), and hashes the body
(sha256, first 12 hex). It then diffs the two multisets of hashes:

- **LOST** — a body that existed at `<gitRef>` and exists nowhere now. This is the failure.
- **NEW** — a body that exists now and did not then. Expected when a move also adds code.
- **unchanged** — the proof. A body that only changed file, name or neighbours still matches.

A pure move, even one that splits a file in two, prints `LOST 0   NEW 0`.

```
$ node tools/moveCheck.mjs HEAD browser/js/core/renderer.js
moveCheck HEAD -> working tree  (1 files then, 1 now)
  unchanged 14   LOST 0   NEW 0
```

## `commentOnlyDiff.mjs` — did a comment sweep touch only comments?

```sh
node tools/commentOnlyDiff.mjs <gitRef> <path...>   # exits 1 if any file CHANGED
node tools/commentOnlyDiff.mjs --self-test
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

## `../desktop/tools/cppCommentDiff.sh` — the same, from the compiler

```sh
desktop/tools/cppCommentDiff.sh <gitRef> <file...>   # exits 1 on any difference
```

A second opinion on a C++ sweep that owes nothing to the hand-rolled scanner: it
preprocesses each side with `gcc -fpreprocessed -dD -E -P` (`-fpreprocessed` leaves includes
and macros alone) and diffs the result. Needs a **real gcc** — Apple's clang-as-gcc rejects
the flag, and the script exits 2 saying so rather than compare two empty files. Use
`commentOnlyDiff.mjs` on macOS.
