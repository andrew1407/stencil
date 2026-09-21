# Extension checklists

The file-by-file procedure for the routine extensions. The architecture itself is in
`ARCHITECTURE.md` and each surface's `ARCHITECTURE.md`; these are the steps, kept out of
those documents on purpose.

### Add an LLM op

1. `browser/js/config/llm/opRegistry.json` — the entry: `keys`, `profiles`, limits, `bullet`.
   Read `opRegistry.README.md` first; the key-spec language is expressive and you rarely need
   a native rule.
2. `cd browser && npm run gen-fixtures` — regenerates the mechanical fixture bundle. Add a
   hand-written fixture under `browser/js/config/llm/fixtures/opPlan/` for the interesting case.
3. Normalizer + executor, per surface in the op's profiles (**the validator is table-driven —
   you write no schema code**): browser `js/llm/opPlan.js` + `js/console/stencilApi.js`;
   desktop `src/llm/opPlan.cpp` + `src/llm/planExecutor.cpp`; cli `src/llm/opplan.zig`;
   mcp `src/opplan/{parse,lower,actions}.rs`; bot `Application/Llm/OpPlanParser*.cs` +
   `PromptService.Actions.cs`; pystencil `pystencil/llm.py`; extension `src/llm/opPlan.js`.
4. `contracts/llm/llm-contract.md` (and the split §-files) — the normative prose.
5. Run every surface's fixture walker. They are the cross-language proof.

### Add a console command (cli, and its pystencil twin)

1. `cli/src/console/commands.zig` — the `Verb` enum member and its word match in the parser.
2. `cli/src/console/handlers.zig` — the handler. It returns values; it does not print.
3. `cli/src/console/ui.zig`, `screen.zig`, `render/` — the rendering. Terminal output lives only here.
4. Help text, then re-record the TUI goldens: `STENCIL_UPDATE_PINS=1 zig build test`.
5. If the command is part of the shared console profile, mirror it in
   `pystencil/pystencil/cli.py`. Update `cli/README.md`.

### Add a script directive

1. `contracts/stc/stc-contract.md` — §3 for the directive and its argument grammar, §8 for
   any new diagnostic code (codes are stable — never re-spell one), §9 if it needs a cap.
2. `core/script/` — the word in `kDirectives` (`scriptParser.cpp`), its grammar in
   `scriptArgs.cpp` (reuse `parseLengthToken`, `parseColor`, `filterModeFromString`,
   `parseCropSpec`), the lowering in `scriptLower.cpp`. A new `OpKind` belongs in
   `scriptTypes.hpp`; a new `.cpp` means **the three source lists**, and a new ABI name means
   `EXPORTED_FUNCTIONS` as well.
3. The JS fallback — `browser/js/core/script*.js` + its `.d.ts`, op-for-op with the C++
   (`DIRECTIVES` is the twin of `kDirectives`). `wasm-parity-script.test.js` is the proof.
4. A fixture pair in `browser/js/config/script/fixtures/cases.txt`: the correct case, and an
   `err-*` case for the way it will most often be written wrong.
5. cli **only if** the directive needs a flag or a console verb — otherwise `--script` already
   runs it through `cli/src/script/apply.zig`.
6. Re-copy `browser/js/core/script*.js` into `vscode-extension/src/parser/` byte-for-byte, and
   add the word to `syntaxes/stc.tmLanguage.json` (its directive list is asserted equal to
   `DIRECTIVES`).
7. The adapters **only if the lowered op is new** — the runners: browser
   `js/console/scriptRunner.js`; desktop `src/app/scriptRun.cpp`; cli `src/script/apply.zig`
   (+ `planActions.zig` for `--script-plan`); pystencil `pystencil/editor/script.py`. A
   directive that lowers to ops that already exist needs none of them.
8. Run every corpus walker — core, browser (fallback **and** wasm parity), pystencil,
   vscode-extension, and the `e2e/` script specs.

### Add a desktop dialog

1. `desktop/src/dialogs/<name>Dialog.{hpp,cpp}` — chrome from `support/modalChrome.hpp`,
   reveal from `support/modalReveal.hpp`. Measure per-state heights in `showEvent`, not from
   a constructor `sizeHint` (a hidden widget's hint is stale).
2. Add the sources to `desktop/CMakeLists.txt`.
3. Wire the action in `desktop/src/app/MainWindowActions.cpp` and `MainWindowMenus.cpp`.
4. A shortcut goes in `browser/js/config/hotkeysConfig.json` — canonical — and reaches the
   desktop through the qrc alias.
5. Styling goes in the shared sheet in `support/theme.cpp`, never `setStyleSheet` on the
   widget.
6. Add a headless test under `desktop/tests/`, then re-pin:
   `STENCIL_UPDATE_UI_PINS=1` on the ui-pins target.

### Add an LLM provider

1. `browser/js/config/llm/providers.json` — id, `displayName`, `defaultBaseUrl`, `chatPath`,
   `wire`.
2. `contracts/llm/llm-providers.md` — the wire mapping, normatively.
3. The mapping in each client, using its platform's built-in HTTP (**no new dependency**):
   browser `js/llm/llmClient.js`; extension `src/llm/llmClient.js`; desktop
   `src/llm/LlmClient.cpp`; cli `src/llm/wire.zig` + `transport.zig`; mcp
   `src/llmtransport.rs`; bot `Infrastructure` ring, `Llm/HttpLlmClient.cs`; pystencil
   `pystencil/llm.py`; server `server/internal/llm/`.
4. Settings UI per surface (browser `js/llm/llmSettings.js`, desktop
   `dialogs/LlmSettingsForm.cpp`, extension `src/llm/llmSettings.js`, cli `/llm`).
5. Fixtures under `browser/js/config/llm/fixtures/providerWire/`, plus each surface's walker.
6. An endpoint is **always explicit user configuration** — never discovered from fetched or
   scanned content. Keys live in env, never in a URL. See `.claude/rules/security.md`.
