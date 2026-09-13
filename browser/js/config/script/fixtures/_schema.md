# `.stc` fixture corpus

The cross-surface proof that every `.stc` parser agrees. All of it lives in **one file**,
`cases.txt`, the way the op-plan corpus keeps its cases in one `cases.json` — a case is a
section, not a file.

```
=== <name>
--- script
<the .stc source>
--- dump
<the canonical program dump>
--- diagnostics          (omitted when the case produces none)
<line:col:len: severity: message [CODE]>
```

Plain text, never JSON: `core/` has no JSON parser, so the C++ walker reads this directly.
A section ends at the next marker, and the blank line before that marker belongs to the
file rather than the case.

**The name carries an expectation.** A case named `err-*` must produce at least one error;
every other case must produce none. Warnings are free either way.

Walked by `core/tests/scriptFixtures.test.cpp` and `browser/tests/scriptFixtures.test.js`
(which share the splitting rules), and by `browser/tests/wasm-parity-script.test.js`, where
the compiled core and the JS fallback must agree case for case.

The four `tour-*` cases double as the language's worked examples; the contract points at
them by name.

To add a case: append a section, run a walker with its update flag, and read the recorded
output back to confirm it says what you meant.
