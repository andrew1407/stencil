<img src="icon.png" width="112" align="right" alt="">

# Stencil — VS Code extension

Editor support for the Stencil script language: a `.stc` file is a recipe — crop this, tint
that, draw a box here, save it there — that Stencil replays over one image or ten thousand.
The extension colours those scripts, completes and explains their vocabulary, underlines
their errors as you type, and runs them through the Zig [CLI](../cli/). It carries no
language rules of its own: the parser it reads a buffer with is a copy of the browser app's,
and a saved file is checked by the same compiled engine that runs it, so the editor and a run
can never disagree. For the project overview see the [repository README](../README.md); for
how the extension is put together, [`ARCHITECTURE.md`](ARCHITECTURE.md).

```stc
# Mark up every screenshot in a folder and file the results next door.
@stencil callout:
    @use line @1, 3px
    @rect (10%,10%) (90%,90%)

@source shots/*.png:
    @use stencil callout #ff3b30:
    @save reviewed/
```

## Editing support

**Highlighting** is driven by the parser, not by a line-at-a-time grammar, so a word is
coloured by the statement it sits in: `@source` as the block it opens, `@save` as the output
it writes, a template name as a name, `sepia` under `@filter` as a filter mode, `dashed`
under `@use line` as a stroke style, `x1` under `@crop` as a crop edge.

**Completion** offers the words that are legal at the caret: the directives on `@`, the
filter modes after `@filter`, the stroke styles after `@use line`, the crop edges after
`@crop`, the CSS colour names, the templates the open file defines, and the units as whole
replacements of a length being typed — `10` becomes `10px`, `10cm` or `10%`.

**Hover** on any directive or keyword gives its meaning, its argument grammar and a worked
example.

**Diagnostics** appear while typing and again on save, each carrying its code
(`E_UNKNOWN_DIRECTIVE`, `W_EMPTY_BLOCK`, …) into the Problems panel.

**Run and Check** are a command, a ⌘⌥R keybinding and a ▶ in the editor title bar.

**File icons** mark both file types in the explorer — the Stencil badge on a `.stc` script,
the bare mark on a `.stencil` project, which also opens as the JSON it is rather than as
plain text.

### Colours

Each family is given a standard semantic token type, so an installed theme colours it with no
setup. Any row can be overridden with `editor.semanticTokenColorCustomizations`.

| What | Token type | Dark Modern |
|---|---|---|
| `@source` | `macro` | blue |
| `@stencil` and its template name, everywhere it is used | `class` / `type` | teal |
| `@use` `@crop` `@filter` `@line` `@rect` `@layout` `@frame` | `keyword` | purple |
| `@save` `@undo` `@redo` | `function` | yellow |
| `@1` `@2` — template parameters | `parameter` | pale blue |
| `bw` `sepia` `invert` `contour` `none` | `enumMember` | bright blue |
| `solid` `dashed` `dotted` `fill` `point` `combine` `replace` | `label` | near-white |
| `x1` `x2` `y1` `y2` `aspect`, and colours | `variable` / `property` | pale blue |
| what `@source`, `@save` and `@layout` name | `string` | orange |
| numbers, then their `px` `cm` `mm` `in` `%` | `number`, `operator` | pale green, grey |

## Examples

**Annotating one image.** A stroke style, a shape over the region of interest, and a save
beside the original under a name of its own.

```stc
@source shot.png:
    @use line 3px #ff3b30
    @rect (12%, 40%) (62%, 58%)
    @save shot-review.png
```

**Treating a folder alike.** A glob `@source` runs its whole block once per file it matches;
a `@save` target ending in `/` collects the results in that directory.

```stc
@source shots/*.png:
    @crop 5%
    @filter bw
    @save out/
```

**Taking a still from a video.** `@frame` selects the frame and starts a fresh set of edits;
the saved name gains a `-frame-<n>` suffix.

```stc
@source clip.mp4:
    @frame 120
    @crop aspect=16:9
    @save
```

**Drawing a saved layout.** `@layout` reads a layout JSON — a grid, a set of guides, a frame
— and draws it over the image, combining with the existing marks unless told to `replace`.

```stc
@source page.png:
    @layout grid.json
    @save
```

**Withdrawing an edit.** Edits are numbered as written, and `@undo` is resolved when the
script is compiled rather than when it runs, so an experiment can stay in the file, inert,
instead of being deleted.

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
after installing, open the Extensions view, find **Stencil** and click **Trust
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
