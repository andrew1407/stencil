# Stencil script for VS Code

Editing support for Stencil `.stc` scripts: syntax highlighting, live error and warning
squiggles, and Run / Check straight from the editor through the [Stencil CLI](../cli/).

## Install

The extension is not on the Marketplace — build the `.vsix` and install it locally.

```bash
cd vscode-extension
npm ci
npm run package          # → stencil-stc.vsix
code --install-extension stencil-stc.vsix
```

Or, in VS Code: **Extensions** → the `…` menu → **Install from VSIX…** → pick
`stencil-stc.vsix`. Reload the window, then open any `.stc` file.

To develop against it instead, open this folder in VS Code and press **F5** — the Extension
Development Host opens with the extension loaded from source.

## Run a script

You need the Stencil CLI on the machine — build it with `cd cli && zig build`, which puts the
binary at `cli/zig-out/bin/stencil`.

| Command | Palette entry | Shortcut | What it does |
|---|---|---|---|
| `stencil.runScript` | Stencil: Run script | `Ctrl+Alt+R` / `⌘⌥R` | `stencil --script <file>` |
| `stencil.runScriptOnImage` | Stencil: Run script on an image… | — | asks for a picture, then `stencil -i <image> --script <file>` |
| `stencil.checkScript` | Stencil: Check script | — | `stencil --script-check <file>` |

There is also a ▶ button in the editor title bar of any `.stc` file. Every command saves the
file first and runs in a terminal called **Stencil**, from the script's own directory — so a
relative `@source` path means what it means on the command line. Output goes wherever the
script's `@save` directives send it.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `stencil.cliPath` | *(empty)* | Path to the `stencil` binary. Empty falls back to the `STENCIL_CLI` environment variable, then `stencil` on your `PATH`. A relative path is taken from the workspace folder. |
| `stencil.checkOnType` | `true` | Re-check the script as you type. Turn it off to check only when you save. |

If no binary can be found, running a command says **Stencil CLI not found — set
stencil.cliPath** and nothing is spawned.

## Errors and warnings

With a CLI configured, saving a file checks it with `stencil --script-check` — the same
compiled engine that runs the script, so the editor never disagrees with a run. While you
type, and whenever no CLI is available, the same checks run inside the editor instead. Errors
are underlined in red, warnings in yellow, and each carries its code (`E_UNKNOWN_DIRECTIVE`,
`W_EMPTY_BLOCK`, …) in the Problems panel. A script with any error runs nothing.

## Test

```bash
npm test
```

Offline, no VS Code download, no test framework — Node's built-in runner.
