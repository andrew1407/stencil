<img src="icon.png" width="104" align="right" alt="">

# Stencil — VS Code extension

Editor support for the Stencil script language. A `.stc` file is a recipe — crop this, tint
that, draw a box here, save it there — that [Stencil](https://github.com/andrew1407/stencil)
replays over one image or ten thousand. The extension colours those scripts, completes and
explains their vocabulary, underlines their errors as you type, and runs them through the
Zig [CLI](https://github.com/andrew1407/stencil/tree/main/cli).

It carries no language rules of its own: the parser it reads a buffer with is a copy of the
browser app's, and a saved file is checked by the same compiled engine that runs it, so the
editor and a run cannot disagree.

- **Repository** — <https://github.com/andrew1407/stencil>
- **The editor in your browser** — <https://andrew1407.github.io/stencil/>
- **The language, normatively** —
  [`contracts/stc/`](https://github.com/andrew1407/stencil/tree/main/contracts/stc)

```stc
# Mark up every screenshot in a folder and file the results next door.
@stencil callout:
    @use line @1, 3px
    @rect (10%,10%) (90%,90%)

@source shots/*.png:
    @use stencil callout #ff3b30:
    @crop 5%
    @filter sepia
    @save reviewed/
```

## Editing support

- **Highlighting** comes from the parser, not a line-at-a-time grammar, so a word is coloured
  by the statement it sits in: `sepia` under `@filter` is a filter mode, `dashed` under
  `@use line` a stroke style, `x1` under `@crop` a crop edge.
- **Completion** offers what is legal at the caret — directives on `@`, filter modes, stroke
  styles, crop edges, CSS colour names, the templates the open file defines, and units as
  whole replacements of a length being typed (`10` → `10px`, `10cm`, `10%`).
- **Hover** gives a directive or keyword its meaning, argument grammar and an example.
- **Diagnostics** appear while typing and again on save, each carrying its code
  (`E_UNKNOWN_DIRECTIVE`, `W_EMPTY_BLOCK`, …) into the Problems panel. A script with any
  error runs nothing.
- **File icons** mark both file types, and a `.stencil` project opens as the JSON it is.

### Colours

Each family takes a standard semantic token type, so an installed theme colours it with no
setup. To set an exact colour for one kind of word, name its family in `stencil.colors`:

```jsonc
"stencil.colors": {
  "filterMode": "#ff8800",   // bw sepia invert contour none
  "source": "#00b4ff"        // @source
}
```

A family left out — or set to `""` — keeps whatever the theme gives it. The value is a hex
colour (`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`); this paints over the theme for `.stc` only.

| Family | Words |
|---|---|
| `source` | `@source` |
| `template` / `templateName` | `@stencil`, and a template's name wherever it is used |
| `edit` | `@use` `@crop` `@filter` `@line` `@rect` `@layout` `@frame` |
| `output` | `@save` `@undo` `@redo` |
| `parameter` | `@1` `@2` |
| `filterMode` | `bw` `sepia` `invert` `contour` `none` |
| `lineStyle` | `solid` `dashed` `dotted` `fill` `point` `combine` `replace` |
| `cropEdge` | `x1` `x2` `y1` `y2` `aspect` |
| `colorValue` | `#ff3b30`, `red`, `transparent` |
| `path` | what `@source`, `@save` and `@layout` name |
| `number` / `unit` / `comment` | numbers, `px` `cm` `mm` `in` `%`, and `#` comments |

To recolour by theme instead of per family — which also reaches every other language — run
**Stencil: Configure highlight colours**, which seeds and opens
`editor.semanticTokenColorCustomizations`. The types it uses are below.

```jsonc
"editor.semanticTokenColorCustomizations": {
  "[*]": {                      // or name one theme, e.g. "[Dark Modern]"
    "rules": {
      "macro":      "#569cd6",  // @source
      "class":      "#4ec9b0",  // @stencil
      "type":       "#4ec9b0",  // a template name, where defined and where used
      "keyword":    "#c586c0",  // @use @crop @filter @line @rect @layout @frame
      "function":   "#dcdcaa",  // @save @undo @redo
      "parameter":  "#9cdcfe",  // @1 @2
      "enumMember": "#4fc1ff",  // bw sepia invert contour none
      "label":      "#c8c8c8",  // solid dashed dotted fill point combine replace
      "variable":   "#9cdcfe",  // x1 x2 y1 y2 aspect
      "property":   "#9cdcfe",  // #ff3b30, red, transparent
      "string":     "#ce9178",  // what @source, @save and @layout name
      "number":     "#b5cea8",
      "operator":   "#d4d4d4"   // px cm mm in %
    }
  }
}
```

These types are shared with every other language, so scope a rule to `.stc` alone by nesting
it under `"[stencil-script]"` in `editor.semanticTokenColorCustomizations` instead.

| | Token type |
|---|---|
| `@source` | `macro` |
| `@stencil` and its template name, wherever used | `class` / `type` |
| `@use` `@crop` `@filter` `@line` `@rect` `@layout` `@frame` | `keyword` |
| `@save` `@undo` `@redo` | `function` |
| `@1` `@2`, crop edges, colours | `parameter` / `variable` / `property` |
| `bw` `sepia` `invert` `contour` `none` | `enumMember` |
| `solid` `dashed` `dotted` `fill` `point` `combine` `replace` | `label` |
| what `@source`, `@save` and `@layout` name | `string` |
| numbers, then their `px` `cm` `mm` `in` `%` | `number` / `operator` |

## Install

The extension is not on the Marketplace — build the `.vsix` and install it locally.

```bash
cd vscode-extension
npm ci
npm run package          # → stencil-stc.vsix
code --install-extension stencil-stc.vsix
```

Reload the window, then open any `.stc` file. Recent VS Code versions gate extensions on
their publisher: if `.stc` files stay plain text, open the Extensions view, find **Stencil**
and click **Trust Publisher**. To work on the extension instead, open this folder and press
**F5** — the Extension Development Host loads it from source, with no packaging or trust
prompt.

## Commands

Running a script needs the Stencil CLI on the machine: `cd cli && zig build` puts it at
`cli/zig-out/bin/stencil`.

| Palette entry | Shortcut | Runs |
|---|---|---|
| Stencil: Run script | `Ctrl+Alt+R` / `⌘⌥R` | `stencil --script <file>` |
| Stencil: Run script on an image… | — | `stencil -i <image> --script <file>` |
| Stencil: Check script | — | `stencil --script-check <file>` |
| Stencil: Configure highlight colours | — | opens the colour setting — see [Colours](#colours) |

There is also a ▶ in the editor title bar. The three script commands save the file first and
run in a terminal called **Stencil**, from the script's own directory — so a relative
`@source` path means what it means on the command line.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `stencil.cliPath` | *(empty)* | Path to the `stencil` binary. Empty falls back to `STENCIL_CLI`, then `stencil` on `PATH`. A relative path is taken from the workspace folder. |
| `stencil.checkOnType` | `true` | Re-check while typing. Off checks only on save. |
| `stencil.highlighting` | `true` | Colour words by the statement they sit in. Off leaves the plain grammar. |
| `stencil.completion` | `true` | Suggest what is legal at the caret. |
| `stencil.hover` | `true` | Explain the word under the pointer. |
| `stencil.colors` | `{}` | An exact colour per family — see [Colours](#colours). Empty leaves every family to the theme. |

With a CLI configured, saving checks the file with `stencil --script-check`. While typing,
and whenever no CLI is found, the same checks run inside the editor instead. The three
toggles take effect on the next keystroke; none of them needs a reload.

What this extension deliberately does not own, because VS Code already does:

| Want | Setting |
|---|---|
| The same recolouring across every language | `editor.semanticTokenColorCustomizations` — **Stencil: Configure highlight colours** opens it |
| Highlighting off for `.stc` only | `"[stencil-script]": { "editor.semanticHighlighting.enabled": false }` |
| Suggestions to stop appearing unprompted | `editor.quickSuggestions`, `editor.suggestOnTriggerCharacters` |
| File icons off | `workbench.iconTheme` — a language icon is drawn only by themes that allow one, so the choice belongs to the theme, not to this extension |

## Test

```bash
npm test
```

Offline, no VS Code download, no test framework — Node's built-in runner.
