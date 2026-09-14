# VS Code extension architecture

The system-wide design — the parity contract, canonical data, the layer model, the pattern vocabulary — is in the root [`ARCHITECTURE.md`](../ARCHITECTURE.md).

An editor adapter for one file type. It gives `.stc` scripts colour, squiggles, completions,
hovers and a Run button, and every one of those answers comes from Stencil itself: the parser copies in
`src/parser/` while you type, the `stencil` CLI on save and on Run. `.stencil` projects are
contributed too, but as data only — an icon and a grammar that defers to `source.json` —
because a saved project is JSON this tree has no reason to interpret. It owns no language
rules of its own — the language lives in [`contracts/stc/`](../contracts/stc/), is
implemented in `core/script/`, and reaches this tree as copied JavaScript and as a spawned
binary.

```mermaid
graph TD
    subgraph VSC["vscode-extension/ (VS Code)"]
      ENTRY["src/extension.js"]
      DIAG["src/diagnostics.js"]
      SEM["src/semanticTokens.js"]
      CMD["src/commands.js"]
      CACHE["src/lib/programCache.js"]
      HOST["src/lib/parserHost.js"]
      COPIES["src/parser/script*.js"]
      GRAM["syntaxes/stc.tmLanguage.json"]
    end
    WEB["browser/js/core/script*.js"]
    CLI["cli/"]

    ENTRY --> DIAG
    ENTRY --> SEM
    ENTRY --> CMD
    DIAG --> CACHE
    SEM --> CACHE
    CACHE --> HOST
    HOST -->|"import()"| COPIES
    WEB -.->|"byte-equal copy"| COPIES
    DIAG -->|"execFile --script-check"| CLI
    CMD -->|"terminal: --script"| CLI
```

## Layers

`src/config/` (data only) + `src/parser/` → `src/lib/` → `src/*.js` → `src/extension.js`.
A layer may use everything to its left and nothing to its right. `src/parser/` is copied
code and imports nothing but itself and the one colour table beside it; `src/lib/` is the
`vscode`-free bottom of this tree's own code, so it is testable without the editor;
`src/*.js` are the three features, each registering itself; `src/extension.js` is wiring and
the only root file that may import a sibling root file. Enforced by
`tests/layerBoundary.test.js` over every relative `import` and `require` in `src/`.

## Where things go

| Path | Holds | Rule |
|---|---|---|
| `package.json` | the manifest VS Code reads: the two languages and their icons, grammars, commands, settings, menu, keybinding, gallery logo | no root `type` field — the extension is CommonJS; `@vscode/vsce` is the one dependency and it is a devDependency (`tests/manifest.test.js`) |
| `language-configuration.json` | `#` line comments, the `()` and `"` pairs, the `@name` word pattern, indent after a `…:` header | its regexes are JS, not Oniguruma — `tests/grammar.test.js` compiles them |
| `syntaxes/stc.tmLanguage.json` | the TextMate grammar under scope `source.stc` | every scope name ends `.stc`; the directive list is pinned to the parser's `DIRECTIVES` |
| `language-configuration.project.json` + `syntaxes/stencilProject.tmLanguage.json` | the `.stencil` project file: brackets, and a grammar whose whole body is `include: source.json` | it defers, never re-spells JSON; contributing it must not add an `activationEvents` entry (`tests/manifest.test.js`) |
| `icons/` | `stencil.svg` (the app mark, the logo's source), `stc.svg` (the mark as a panelled badge, the script's explorer glyph) and the light/dark bare-stroke pair for a project | `stencil.svg` is a copy of `browser/favicon.svg`, pinned by `browser/tests/svgArt.test.js`; the glyphs are this surface's own art. `stc.svg` carries its own panel, so one file serves both themes; the bare stroke does not, so it is a pair |
| `icon.png` | the extension-page logo, rasterized from `icons/stencil.svg` | PNG, ≥128px — VS Code refuses an SVG here |
| `src/extension.js` | `activate` / `deactivate` | wiring only; every disposable goes on the context |
| `src/diagnostics.js` | the two diagnostic sources, the debounce and the version guard | the CLI answers for a saved file, the parser copies for a buffer; both become `vscode.Diagnostic` |
| `src/semanticTokens.js` | the legend and the provider | standard VS Code token types only, so any theme colours it; WHICH type a token gets is `lib/tokenClassify.js` |
| — | each of the three providers reads its own `stencil.*` toggle per request | a toggle must not need a reload, so nothing is decided at `register` time; colouring, recolouring and the icon themes stay VS Code's own settings |
| `src/completion.js` | the suggestion items, one builder per group | it offers words, never a filename — a path is the user's to type |
| `src/hover.js` | the Markdown for the token under the caret | the token comes from the same parse the colours do, so a hover cannot land where no colour did |
| `src/decorations.js` | the `stencil.colors` overrides: one `TextEditorDecorationType` per family the user named, painted over the themed tokens | a theme decides what a token type looks like, so an EXACT colour can only be drawn on top; a family left unset gets no decoration at all, so the theme still owns it |
| `src/colors.js` | the one command that is not a CLI invocation: it seeds and opens VS Code's token-colour setting | an extension may not set token colours, so this hands the user the setting rather than owning one; the seeded rules are pinned to the README block (`tests/colors.test.js`) |
| `src/commands.js` | run, run-on-image, check | one CLI invocation each, in the reused `Stencil` terminal, `cwd` = the script's directory |
| `src/lib/` | `ids.js` (the contributed identifiers), `cliLocator.js` (the ONE way the binary is found) over `pathSearch.js` (the executable probe and the memoized PATH walk), `terminal.js` (the ONE place a command line is composed) over `shellQuote.js` (the per-shell rules), `scriptCheck.js` (the CLI's `--script-check` answer and its line grammar), `parserHost.js` (the memoized `import()`) and `programCache.js` over it (one parse per document version), `vocabulary.js` (the one reading of the vocabulary table), `tokenClassify.js` (a legend type per token, from the statement it sits in), `colorFamilies.js` (the user-facing name for each legend type, and the reduction of `stencil.colors`) and `completionContext.js` (which suggestion groups a caret takes) | `vscode` is passed in, never imported, so each is a pure unit |
| `src/parser/` | byte-equal copies of `browser/js/core/script*.js`, plus `index.js`, which re-composes what `script.js` does without the wasm binding | ESM, scoped by its own `package.json`; pinned both directions by `tests/parserParity.test.js` |
| `src/config/` | `colorNames.json`, the one table the copies import, and `stcVocabulary.json` | `colorNames.json` is byte-pinned to `browser/js/config/colorNames.json`; the vocabulary is this surface's own — no other surface explains the language to a reader — and its directive keys are held to the parser's `DIRECTIVES` |
| `tests/` | `node --test` suites and `helpers/vscodeStub.js` | ESM, scoped by its own `package.json`; no editor, no network |

## Entities

```mermaid
classDiagram
    class ScriptProgram { +ScriptToken[] tokens; +ScriptDiagnostic[] diagnostics; +ScriptBlock[] blocks; +ScriptOp[] ops; +boolean hasErrors }
    class ScriptToken { +number line; +number col; +number len; +TokenKind kind; +string text }
    class ScriptDiagnostic { +Severity severity; +string code; +number line; +number col; +number len; +string message }
    class DiagnosticEntry { +number line; +number col; +number len; +string severity; +string message; +string code }
    class CliLocation { +string configured; +string baseDir; +Env env }
    class CommandLine { +string cli; +string[] args; +string cwd }
    class ExtensionSettings { +string cliPath; +boolean checkOnType }

    ScriptProgram *-- "0..*" ScriptToken : tokens
    ScriptProgram *-- "0..*" ScriptDiagnostic : diagnostics
    ScriptDiagnostic --> DiagnosticEntry : fromProgram
    DiagnosticEntry --> "1" ScriptToken : span
    ExtensionSettings --> CliLocation : cliPath
    CliLocation --> CommandLine : the resolved binary
    ScriptToken --> "0..1" CommandLine : never
```

| Entity | What it is | Owned by / lifetime | Relates to |
|---|---|---|---|
| `ScriptProgram` (`src/parser/index.js`) | one parse of a `.stc` buffer: tokens, diagnostics, blocks, ops; canonical in `core/script/scriptProgram.cpp` | built once per document version, held by `programCache` for the last few versions | `ScriptToken`, `ScriptDiagnostic` |
| `ScriptToken` (`src/parser/scriptLexer.js`) | one lexeme with its 1-based line, column, length and `TokenKind` | inside a `ScriptProgram` | becomes a semantic-token row through `KIND_TYPE` |
| `ScriptDiagnostic` (`src/parser/scriptDiagnostics.js`) | one error or warning with its stable `E_`/`W_` code and span | inside a `ScriptProgram` | flattened into a `DiagnosticEntry` |
| `DiagnosticEntry` (`src/diagnostics.js`) | the shape both sources agree on — the parser's diagnostics and the CLI's `--script-check` lines | per refresh; mapped straight to `vscode.Diagnostic` | `ScriptDiagnostic`, the CLI's stdout |
| `CliLocation` (`src/lib/cliLocator.js`) | the inputs to finding the binary: the `stencil.cliPath` setting, the workspace folder, the environment | per call; only the PATH walk under it is memoized, briefly and per `PATH` | `ExtensionSettings`; produces the path a `CommandLine` runs |
| `CommandLine` (`src/lib/terminal.js`) | one composed, fully quoted shell line and the terminal it is sent to | per command invocation; the `Stencil` terminal outlives it | `ShellRules`, and the CLI process |
| `ShellRules` (`src/lib/shellQuote.js`) | one shell family's quoting: what needs no quotes, how a quote is escaped, how a directory is changed, what a quoted command word needs in front of it | a frozen table entry, chosen per invocation from `vscode.env.shell` | `CommandLine` |
| `ExtensionSettings` | `stencil.cliPath` and `stencil.checkOnType`, read through `workspace.getConfiguration` | VS Code's, read on each use so a change needs no reload | `CliLocation`, the on-type check |

## Patterns

| Pattern | Where | Notes |
|---|---|---|
| Port (byte-equal copy) | `src/parser/script*.js` ← `browser/js/core/script*.js`; `src/config/colorNames.json` | The extension cannot import across subprojects, so the parser is copied and `tests/parserParity.test.js` pins it both directions: no file may appear, vanish or change alone. |
| Adapter | `src/diagnostics.js` `parseCheckOutput` + `toDiagnostic` | The CLI's one-line-per-diagnostic grammar and the parser's objects both become `vscode.Diagnostic`. |
| Strategy | `src/diagnostics.js` `collect` | Saved file plus a locatable CLI takes the compiled core; anything else takes the copies. The two must agree, which is what the shared corpus proves. |
| Table-driven | `src/semanticTokens.js` `KIND_TYPE`; `src/lib/ids.js` | A `TokenKind` becomes a legend index by lookup, and every contributed id has one home the manifest test reads. |
| Chain of Responsibility | `src/lib/cliLocator.js`: setting → `STENCIL_CLI` → `PATH` | Each step refuses or answers; no shell is consulted, so nothing is word-split or expanded. |
| Lazy singleton | `src/lib/parserHost.js` | One memoized `import()` bridges CommonJS to the ESM copies; both features share the module graph. A rejection is never memoized, so one failure does not outlive itself. |
| Strategy (table) | `src/lib/shellQuote.js` `SHELLS` | PowerShell, cmd.exe and POSIX each get a row; `vscode.env.shell` picks it. Quoting is never re-derived at a call site. |
| Cache | `src/lib/programCache.js`; the PATH walk in `src/lib/pathSearch.js` | Keyed on what invalidates it — a document's `version`, and the whole `PATH` — so a keystroke lexes once for both features and a burst of opens walks `PATH` once. |
| Facade | `src/extension.js` | Three `register(context)` calls; no feature knows another exists. |

## Design

- **Activation.** VS Code loads `src/extension.js` on `onLanguage:stencil-script`. `activate`
  calls `diagnostics.register`, `semanticTokens.register` and `commands.register`; each pushes
  its own disposables onto the context and returns. Nothing is loaded eagerly — the parser
  copies arrive on the first parse, through `parserHost`.
- **Colour.** Two layers. The TextMate grammar paints as the file loads, line by line, and is
  what a `.stc` looks like before the extension activates. The semantic-token provider then
  re-paints from a real parse, which is how `#ccc` stays a colour while `# note` is a comment —
  a decision the lexer makes from the whole word and a regex can only approximate.
- **A check.** On open and on save, `collect` locates the CLI and runs
  `execFile(cli, ['--script-check', path])` with no shell, so the extension host is never
  blocked; each output line is read by `CHECK_LINE` into a `DiagnosticEntry`. A run that did
  not answer about the script — an exit status other than 0 or 1, or a failure that printed
  nothing parsable — is **no answer at all**, not an empty one, so the copies take over rather
  than the squiggles silently clearing. While typing (`stencil.checkOnType`, default on) the
  copies answer anyway, debounced per document, and a result whose `version` the next keystroke
  has already outdated is dropped instead of painted. Either way `toDiagnostic` turns 1-based
  spans into 0-based ranges, never zero-width, tagged `source: 'stencil'` and carrying the
  `E_`/`W_` code.
- **A run.** `stencil.runScript` saves the buffer, locates the CLI, and sends
  `stencil --script <file>` to the reused `Stencil` terminal with `cwd` set to the script's
  own directory, so a relative `@source` resolves the way it does on the command line.
  `stencil.runScriptOnImage` prefixes `-i <picked image>`; `stencil.checkScript` sends
  `--script-check`. Every argument goes through `quoteArg` first, under the `ShellRules` for
  the shell VS Code reports. With no CLI the command refuses with "Stencil CLI not found — set
  stencil.cliPath" and spawns nothing; a buffer with no file behind it — an untitled one, or a
  Save As the user cancelled — refuses too, because `fsPath` would be a label, not a path.
- **The parser copies.** `src/parser/` is `browser/js/core/script*.js`, byte for byte, with
  two files left behind: `script.js`, whose imports reach the wasm loader and the app's unit
  helpers, and `scriptHandles.js`, which marshals wasm handles. `src/parser/index.js` stands in
  for the first, re-composing lex → parse → lower exactly as `script.js` does when wasm is
  absent; `tests/parserParity.test.js` pins those declarations line for line. The copies are
  ESM and the extension is CommonJS, so the only way in is `parserHost`'s dynamic `import()`.

## Rules

1. **The CLI path is explicit user configuration.** `stencil.cliPath`, then `STENCIL_CLI`,
   then `PATH` — and never a path read out of the document being edited or out of anything
   the script fetches.
2. **Document text never reaches a shell unquoted.** `src/lib/terminal.js` is the only place
   a command line is composed, and it composes for the shell the user actually runs, never for
   an assumed POSIX one; every spawn elsewhere takes an argv array and `shell: false`. A path
   holding a quote, a space, a percent or a semicolon survives as one argument.
3. **The parser is copied, never edited here.** A change belongs in `core/script/` and
   `browser/js/core/`; this tree re-copies. `tests/parserParity.test.js` fails on any drift,
   in either direction, and `tests/sizeBudget.json` `exceptions` marks the copies as copies.
4. **The grammar follows the language, not the other way round.** The directive list is
   asserted equal to the parser's `DIRECTIVES`; a new directive lands in the contract and the
   core first.
5. **One dependency, dev-only.** `@vscode/vsce`, exactly pinned, used only by
   `npm run package`. Nothing ships at runtime; the packaged `.vsix` carries `src/` and
   `syntaxes/` and no `node_modules`.
6. **Every disposable belongs to the context.** `deactivate` does nothing, because there is
   nothing left for it to do.

## Tests

`tests/` runs under `node --test`, offline and without VS Code: `helpers/vscodeStub.js`
answers `require('vscode')` through a `Module._load` hook with a recording stub, so the
extension's own wiring, the diagnostic collection, the semantic-token rows and the terminal
lines are all asserted from the outside. The quoting is proved per shell family — and the POSIX
line additionally round-tripped through a real `/bin/sh` — while a stub CLI standing in for
every exit status proves which answers fall back to the copies. Cross-surface drift is pinned, not re-tested:
`parserParity.test.js` holds `src/parser/` byte-equal to `browser/js/core/` both ways and
pins the three declarations `index.js` re-composes, while `fixtureWalker.test.js` replays the
shared corpus in `browser/js/config/script/fixtures/cases.txt` — the same file the core and
the browser walk — through the copies. Data is asserted as data: `grammar.test.js` compiles
every TextMate and language-configuration regex, resolves every `include`, and checks the
scope names and the directive list; `tokenClassify.test.js` and `hover.test.js` run real buffers through the
parser copies rather than hand-built spans, so a colour or an explanation is asserted where a
reader would see it, and `vocabulary.test.js` holds the documented words to the parser's own
lists. `manifest.test.js` holds `package.json` to `src/lib/ids.js`,
to the files it points at — both languages' configurations, grammars and icons, and the
logo — and every module to its sibling `.d.ts`. `layerBoundary.test.js` scans import direction and
`sizeBudget.{json,test.js}` is the line and comment ratchet. There is deliberately **no
`@vscode/test-electron` end-to-end suite**: it would download a VS Code build per run, which
this repo's no-new-dependency rule rules out, and the behaviour it would cover is the editor's
own. The editor-side check is manual — install the `.vsix`, open a fixture `.stc`.
