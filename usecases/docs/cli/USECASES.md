# CLI — use cases

The Zig command-line tool, one-shot and interactive. Build it from
[`cli/README.md`](../../../cli/README.md).

## One-shot edits

`stencil --help` prints the flags: a source (a file, a URL, a video frame or a blank page), the
edits (crop, flip, rotate, filter, layout, script), and the output path.

![stencil --help](img/help.png)

A run reads the source, applies the edits and answers with one line:
`wrote <path> (<width>x<height> px · <page>)`, or `error:` and what went wrong.

![a crop, a quarter turn and sepia in one command, then a missing file](img/oneshot.png)

![the picture it wrote](img/oneshot-result.png)

## Edit a folder with a script

A `.stc` script whose `@source` names a folder runs on every picture in it, and a `@save` that
names a folder writes each result there as `<name>-stencil`, in the picture's own format.

![a script over photos/ and the three pictures it wrote](img/batch.png)

## Draw a layout

`-l` draws a layout JSON — lines with their points, colours, widths and styles — over the
picture. Its points are in the picture's own pixels after the crop and turns; `--layout-frame
source` reads them in the original picture instead.

![a layout drawn over the logo](img/layout.png)

![the picture with the layout on it](img/layout-result.png)

## Take a frame from a video

`--probe` describes a video without writing anything: its format, size, length and frame count.
`-f` picks the frame a run opens; without it, a video opens on its first frame.

![a clip probed, then its frame 30 saved](img/video.png)

## Share through a server

`--remote <url>` uploads the result as a new project on a collaboration server, named after the
input or by `--remote-name`. `--server <url>` with `-i <project name>` opens a server project
instead, and `--remote-update` writes the result back into it. The token comes from `--token`, or
from the environment: `STENCIL_SERVER_TOKENS` (one per server) or `STENCIL_SERVER_TOKEN`.

![a picture uploaded as a new server project, then turned and written back](img/server.png)

## The full-screen console

`stencil --console-full-screen` opens a console with the logo pinned at the top, the output
under it and the prompt at the bottom; the plain `--console` is the same conversation without
the screen.

![the console, idle](img/term-idle.png)

## Load a picture and edit it step by step

`/upload <path|url>` asks `Upload …? (Y/n)` before it loads, and the console then names the
picture; a video opens on its first frame. `/status` describes what is loaded. `/crop`, `/flip`,
`/rotate` and `/filter` take the same arguments as the flags, and every step answers with the
new size and its place in the edit history (`[3/4]`: step 3 of 4). `/undo` steps back, `/redo`
forward, and `/save` writes the result.

![a picture loaded from a link, cropped, warmed, turned, the turn undone, saved](img/term-session.gif)

![the same session when it is done](img/term-session.png)

## Start from a blank page and run a script

`/blank a5 black` makes a page. `/script` runs `.stc` statements on the open picture,
`;`-separated, as one edit — so a single `/undo` takes the whole script back.

![a blank page, a script drawn on it, and one /undo](img/term-blank.gif)

## Ask the assistant

With `STENCIL_LLM_*` pointing at a model, `/prompt` describes an edit in words. The plan runs on
the working picture step by step, and each variant it asks for is written to the working
directory as `variant-<label>.png`.

![a /prompt turn: the answer, its steps, and the two variants it wrote](img/term-prompt.png)

## Hand the picture to the other apps

`/browser` opens the session in the browser app, at its default address unless
`STENCIL_BROWSER_URL` or the command names another. `/desktop` opens it in the desktop app: a
server project by its link, anything else as a `.stencil` file the command writes first.

![/browser and /desktop with a picture loaded](img/term-handoff.png)

## Every command

`/help` lists the commands, grouped: image, edit, save, connections, apps, assistant and system,
with the keyboard shortcuts under them.

![the /help listing](img/console-help.png)

`/format` lists the page formats, and `/theme` the accent colours, each name in its own colour —
the dark ones read best in a light terminal:

![the page formats](img/formats.png)

| In a dark terminal | In a light terminal |
|---|---|
| ![the accent colours, dark](img/themes-dark.png) | ![the accent colours, light](img/themes-light.png) |

`/projects` lists the projects on every connected server; `/connect <url> [token]` adds one.

![/projects across two servers](img/projects.png)

## Change the accent

Click the logo to cycle the accent colour; the recolour sweeps across the header, the edit log
and the prompt.

![two clicks on the logo, each a new accent](img/term-theme-cycle.gif)

`/theme` takes an accent by name, any CSS colour name, or a `#hex`.

![/theme turquoise, then /theme #e3b341](img/term-theme.gif)

Output reveals line by line; `/reveal-speed` sets the pace, from `0.01` to `1` (instant).

![/reveal-speed 0.08, then /format revealed](img/term-reveal.gif)
