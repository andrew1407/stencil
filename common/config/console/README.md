# `common/config/console/` — the consoles' shared tables

Three tables the Zig CLI's console, the browser's and the desktop's share, so no surface retypes
a command, a secret or a tempo.

| File | Holds | Who reads it |
|---|---|---|
| `commands.json` | the GUI consoles' commands: name, aliases, argument hint, help line, section, `surfaces`, `cliMode` rows that answer only inside the secret CLI mode; the `cliMode` block names the typed word that opens it, its logo-hold ms and the command that leaves it | browser `js/console/shell/table.js` (served), desktop `support/console/consoleGrammar.cpp` (qrc). Each proves its rows match its handlers |
| `skins.json` | the secret skins: one row per Zig `Skin` tag with its word, phrases, wordmark, traits and colours, plus the pictures, cat faces and bulbs they draw | the CLI at compile time (`app/skin/table.zig`, pinned by `tests/config/skins_drift_test.zig`), browser `js/ui/cli/skin/`, desktop `app/cli/CliSkins.cpp` |
| `fx.json` | the TUI's animation numbers: the reveal, the accent wipe, the press frame, the matrix rain | the browser's and desktop's CLI mode; the CLI keeps its constants beside the code and pins them with `tests/config/console_fx_drift_test.zig` |

The Zig console's own command list and help stay in `cli/src/console/uiText.txt`; the
"Console text speed" range the GUIs reveal at is `consoleSpeed` in `../motion.json`.
