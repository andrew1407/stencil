<img src="icon.png" width="112" align="right" alt="">

# Stencil script for VS Code

**Edit pictures the way you edit code.** A `.stc` script is a short, readable recipe — crop
this, tint that, draw a box here, save it there — that Stencil replays over one image or ten
thousand. This extension gives those scripts colour, live error squiggles and a Run button,
and every answer it shows comes from Stencil itself: the same engine that runs the script
tells the editor what is wrong with it, so the two can never disagree.

```stc
# Mark up every screenshot in a folder and file the results next door.
@stencil callout:
    @use line @1, 3px
    @rect (10%,10%) (90%,90%)

@source shots/*.png:
    @use stencil callout #ff3b30:
    @save reviewed/
```

## What you get

- **Syntax highlighting that reads the statement, not just the line.** `@source` is coloured
  as the block it opens, `@save` as the output it writes, the edits as edits, a template name
  as a name — and `sepia` under `@filter`, `dashed` under `@use line` and `x1` under `@crop`
  each get their own colour, because the parser says which is which. Standard token types
  only, so whatever theme you already use colours it.
- **Completions** for every one of those words — directives on `@`, the five filter modes
  after `@filter`, the stroke styles after `@use line`, the crop edges after `@crop`, every
  CSS colour name, the templates your own file defines, and `10` → `10px` / `10cm` / `10%`.
- **Hover** any directive or keyword for what it means, its syntax and a worked example.
- **Errors and warnings as you type**, each with its code (`E_UNKNOWN_DIRECTIVE`,
  `W_EMPTY_BLOCK`, …) in the Problems panel.
- **Run and Check from the editor** — ⌘⌥R, or the ▶ in the title bar.
- **File icons** in the explorer: the Stencil badge on a `.stc` script, the bare mark on a
  `.stencil` project — which now opens as the JSON it is, rather than plain text.

## What people use it for

**Annotating a screenshot for a bug report.** A red box over the broken control, saved beside
the original, in one keystroke — and the same script again next week when it regresses.

```stc
@source shot.png:
    @use line 3px #ff3b30
    @rect (12%, 40%) (62%, 58%)
    @save shot-review.png
```

**Preparing a folder of images at once.** One script, a glob, and every file gets the same
treatment. This is the job that does not fit in an image editor.

```stc
@source shots/*.png:
    @crop 5%
    @filter bw
    @save out/
```

**Pulling a still out of a video.** Pick the frame, crop it to the aspect you need, done.

```stc
@source clip.mp4:
    @frame 120
    @crop aspect=16:9
    @save
```

**Laying a design over a page.** `@layout` draws a saved layout — a grid, a set of guides, a
frame — over the image, so a batch of pictures can be dropped into the same template.

```stc
@source page.png:
    @layout grid.json
    @save
```

**Trying something and taking it back.** Edits are numbered as written, and `@undo` resolves
when the script is compiled — so you can leave an experiment in the file, disabled, instead of
deleting it.

```stc
@source a.png:
    @filter sepia
    @rect (10%,10%) (90%,90%)
    @undo
    @save
```

The language itself — every directive, unit and diagnostic — is written up in
[`contracts/stc/`](../contracts/stc/).

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

Recent VS Code versions gate extensions on their publisher. If `.stc` files stay plain text
after installing, open the Extensions view, find **Stencil script (.stc)** and click **Trust
Publisher** — an untrusted publisher's extension contributes nothing, not even a file type.

To develop against it instead, open this folder in VS Code and press **F5** — the Extension
Development Host opens with the extension loaded from source, no packaging and no trust
prompt.

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
are underlined in red, warnings in yellow, and each carries its code in the Problems panel. A
script with any error runs nothing.

## Test

```bash
npm test
```

Offline, no VS Code download, no test framework — Node's built-in runner.
