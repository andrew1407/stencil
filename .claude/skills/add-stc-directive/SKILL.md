---
name: add-stc-directive
description: >-
  The file-by-file procedure for adding or changing a `.stc` script directive (an `@word`) or
  its argument grammar in Stencil — the contract, the C++ parser/lowerer in core/script/, the
  JS fallback, the fixture corpus, the VS Code parser copy and grammar, and the runners. Use
  when asked to add a directive, a new argument form, a diagnostic code or a cap to the .stc
  language.
---

# Add a script directive

One parser (`core/script/`) serves every surface; the JS fallback and the VS Code copy follow
it op for op, and `common/fixtures/script/cases.txt` proves all of them.

1. `contracts/stc/stc-contract.md` — §3 for the directive and its argument grammar, §8 for any
   new diagnostic code (codes are stable — never re-spell one), §9 if it needs a cap.
2. `core/script/` — a `Directive` member and its word in `DIRECTIVE_WORDS`
   (`core/script/parser.hpp`: bump the array size, keep the JS order, and an edit directive
   sits inside the contiguous `CROP..LAYOUT` run), its grammar in
   `core/script/args.cpp` (reuse `parseLengthToken`, `parseColor`, `filterModeFromString`,
   `parseCropSpec`), the lowering in `core/script/lower.cpp`. A new `OpKind` belongs in
   `core/script/types.hpp`. A new `.cpp` means **the three source lists** (and a new folder
   the include-dir lists); a new ABI name means `EXPORTED_FUNCTIONS` as well —
   `.claude/rules/core-changes.md` names them. Never `throw` in core.
3. The JS fallback — the modules in `browser/js/core/script/` + their `.d.ts`, op-for-op with
   the C++ (`DIRECTIVES` is the twin of `DIRECTIVE_WORDS`).
   `browser/tests/wasm/wasm-parity-script.test.js` is the proof.
4. A fixture pair in `common/fixtures/script/cases.txt`: the correct case, and an
   `err-*` case for the way it will most often be written wrong.
5. cli **only if** the directive needs a flag or a console verb — otherwise `--script` already
   runs it through `cli/src/script/apply.zig`.
6. `node tools/syncTwins.mjs browser/js/core/script/` — re-copies the parser into
   `vscode-extension/src/parser/script/` byte for byte — and add the word to
   `vscode-extension/syntaxes/stc.tmLanguage.json` (its directive list is asserted equal to
   `DIRECTIVES`).
7. The adapters **only if the lowered op is new** — the runners: browser
   `browser/js/console/scriptRunner.js`; desktop `desktop/src/app/scriptRun.cpp`; cli
   `cli/src/script/apply.zig` (+ `cli/src/script/plan/sequence.zig` for `--script-plan`);
   pystencil `pystencil/pystencil/editor/script.py`. A directive that lowers to ops that
   already exist needs none of them.
8. Run every corpus walker — core, browser (fallback **and** wasm parity: rebuild wasm first,
   a nonzero `skipped` proved nothing), pystencil, vscode-extension, and the `e2e/` script specs.
