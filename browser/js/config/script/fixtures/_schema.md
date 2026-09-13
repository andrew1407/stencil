# `.stc` fixture corpus

The cross-surface proof that every `.stc` parser agrees. Each case is up to three files:

| File | Holds |
|---|---|
| `<name>.stc` | the script — the only hand-written file |
| `<name>.dump.txt` | the canonical program dump (`scriptDump`), always present |
| `<name>.diag.txt` | the expected diagnostics, present only when the script produces any |

Plain text, never JSON: `core/` has no JSON parser, so the C++ walker reads these directly.

**The name carries an expectation.** A case named `err-*` must produce at least one error;
every other case must produce none. Warnings are free either way.

Walked by `core/tests/scriptFixtures.test.cpp`, `browser/tests/scriptFixtures.test.js`,
`cli/tests/script_fixtures_test.zig`, `pystencil/tests/test_fixture_script.py` and
`vscode-extension/tests/fixtureWalker.test.js`.

The four `tour-*` cases double as the language's worked examples; they are what
`stc-contract/stc-contract.md` and the surface READMEs point at.

To add a case: write the `.stc`, run any walker with its update flag, and read the recorded
output back to confirm it says what you meant.
