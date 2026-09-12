---
name: stencil
description: >-
  Edit an image (or a video frame) headlessly using Stencil's own core, then
  save the result. Use when asked to crop, rotate (quarter-turns), tint/filter
  (b&w / sepia / duotone color), draw a layout over, extract a video frame from,
  or create a blank page — for one or more local files or http(s) URLs. Also use
  when the user invokes /stencil explicitly. Drives the Zig CLI (cli/), which
  wraps the shared C++ core, so results match the browser and desktop editors.
allowed-tools:
  - Bash
  - Read
  - Write
  - Edit
---

# Stencil image/video editing

Drive Stencil's headless CLI (`cli/`, a Zig wrapper over the shared C++ `core/`) to
transform an image or a video frame and write the result — no GUI, and every pixel/
geometry operation is the same code the browser and desktop editors run.

## Invocation forms

The user may call `/stencil` (or describe the task in prose) in any of these shapes —
figure out which by what's present:

- `/stencil <input> <output> <actions…>` — explicit input, explicit output, actions.
- `/stencil <input> <actions…>` — derive a sensible output path (see Output).
- `/stencil <actions over one or more files>` — parse the inputs out of the prose.

`<input>` is a local path or an `http(s)://` URL to an image **or a video**.
`<output>` may be an image file (extension optional — it's auto-filled from the
input format) **or** a `*.json` file, which means: write the generated layout JSON
instead of rendering (see "Drawing / layout"). When multiple inputs are given,
apply the actions to each and write one output per input.

If the request is ambiguous (no input found, or you can't tell output from action),
ask one concise clarifying question before running anything.

## How to run

1. **Locate the repo + CLI.** The repo root is the nearest ancestor containing
   `cli/build.zig` (from `browser/` it's `..`). The built binary is
   `cli/zig-out/bin/stencil`.
2. **Build if missing.** If the binary isn't there, run `zig build` in `cli/`
   (first build fetches the `stb` headers, needs network once). If `zig` isn't on
   `PATH`, tell the user — offer the Docker path (`docker build -f cli/Dockerfile
   -t stencil-cli . && docker run --rm -v "$PWD:/work" -w /work stencil-cli …`,
   built from the repo root) — and stop.
3. **Translate the request into one CLI call per input** (mapping below), run it,
   then report the absolute output path(s) and what was applied. Don't overwrite an
   existing file the user didn't name as the output without confirming.

## Choosing the command

```
stencil [options] <output>
```

**`cli/zig-out/bin/stencil --help` is the exhaustive flag list** — read it instead of
guessing. What you need to *choose* a command:

| User asks for | Flag |
|---|---|
| use this image/video | `-i, --input <path\|url>` (local file or `http(s)://`) |
| blank canvas | `--blank [fmt] [w h] [color]` — a page format (`a0`…`c10`) **or** explicit `w h`, not both; default A4, default white |
| a frame from a video | `-f, --frame <n>` (0-based; needs `ffmpeg` on PATH; direct media, not a streaming page URL) |
| crop | `-c, --crop "x1=… x2=… y1=… y2=…"` — each edge is `px`/`cm`/`mm`/`in`/`%` or a bare pixel delta; a leading `-` measures from the far edge; omit an edge to keep the image bound |
| keep page aspect on a 1-axis crop | `--album` |
| rotate | `-r, --rotate <int>` |
| draw something on it | `-l, --layout <path\|url>` (see below) |
| b&w / negative / edges / sepia / tint | `--filter <bw\|invert\|contour\|sepia\|color>` — a colour name or `#hex` makes a duotone tint |
| don't let the output escape the cwd | `--confine-output` |
| (result file) | `<output>`, positional and last |

Non-obvious rules the flag list won't tell you:

- **Rotation is quarter-turns only** (`int × 90°`: `1` = 90° CW, `-1` = CCW, `2` = 180°).
  Arbitrary angles aren't supported — say so if asked.
- `-i` and `--blank` are **mutually exclusive**; so is a page format vs. explicit `w h`.
- `--filter` **overrides** a filter set inside the layout JSON.
- Order doesn't matter to the CLI; the pipeline always runs
  **source → crop → rotate → filter → layout → encode**.
- `--layout-frame <current|source>` says which image the layout's coordinates are in.
  Default `current` = the already-cropped/rotated image. Pass `source` when you computed
  points against the *original* and want them re-mapped through the crop/rotation.
- `--confine-output` refuses an output path that leaves the working directory (absolute,
  or a leading `~`; `..` is refused either way). Off by default. Pass it when the path
  came from somewhere other than the user — that's why the mcp and bot adapters set it.

### Server projects

`--server <url>` makes `-i <name>` fetch a **project** from a [collaboration
server](../../../server/README.md) instead of reading a local path (it *requires* `-i`, so it
can't be combined with `--blank`); `--remote-update` writes the result back into it;
`--remote <url>` (+ `--remote-name <name>`) publishes the result as a **new** project;
`--token <tok>` authenticates against a server that gates token minting. `--server` and
`--remote` may be **different** servers, so one run can copy a project between them. The
result is always saved locally too. For an interactive multi-server session
(`/connect`, `/fetch`, `/sync`, live update notices) use `--console`; see `cli/README.md`.

### Examples
```bash
# center-crop to 80% and rotate a quarter-turn clockwise
cli/zig-out/bin/stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png
# blank red 800x600, draw a saved layout, tone it sepia
cli/zig-out/bin/stencil --blank 800 600 red --layout notes.json --filter sepia out
# blank on a named page format
cli/zig-out/bin/stencil --blank b5 pink page.png
# grab the 24th frame of a video as a still
cli/zig-out/bin/stencil -i clip.mp4 -f 24 frame.png
# duotone tint an image fetched from a URL
cli/zig-out/bin/stencil -i https://example.com/pic.png --filter "#7c3aed" tinted.png
# fetch server project "Shared", tone it sepia, write the result back to it
cli/zig-out/bin/stencil --server http://host:8090 -i Shared --filter sepia --remote-update out.png
# publish a local image as a new server project after rotating it
cli/zig-out/bin/stencil -i photo.png -r 1 --remote http://host:8090 --remote-name "Shared" out.png
```

Note: an MCP client reaches the same server-project actions through the `stencil_edit`
tool's `server` / `remote_update` / `remote` / `remote_name` parameters (`mcp/README.md`).

## Scrape a source site (headless media download)

For "grab the images/videos off this page" requests, `--source-site <url>` switches the CLI
into **scrape mode**: it fetches the page over HTTP, parses the HTML (no browser), extracts
image/video/background/poster media URLs, filters them, and **downloads the matches into an
output DIRECTORY** — here the positional `<output>` is a *destination folder* (created if
missing, default `.`), **not** a single rendered image. Scrape mode ignores the editing flags
above (crop/rotate/layout/filter/frame) and the server flags, and is mutually exclusive with
`-i` / `--blank`.

Narrowing flags (`--help` has the full list): `--source-count <N>` items per group
(**default 5**; `0` = all matches) with `--group <G>` the 0-based page index over the
filtered list (`filtered[G*N : G*N+N]`); `--source-filter` (`img|video|background|poster`),
`--source-format` (`png|jpg|webp|mp4|…`) and `--source-name <regex>` (POSIX ERE,
case-insensitive) select what to keep; `--source-min-width` / `--source-max-width` /
`--source-min-height` / `--source-max-height` bound the pixel size inclusively (`0` = unset;
only images are measured, unknown-size items pass). Absent filters mean `all`.

Each downloaded file prints a `wrote …` line to stderr and the run ends with a
`scraped {n} file(s) from {host} into {dir}` summary (per-item fetch failures are non-fatal;
zero matches is a hard error).

```bash
# download the first 10 PNG/JPG images at least 200px wide into ./shots/
cli/zig-out/bin/stencil --source-site https://example.com \
  --source-filter img --source-format "png|jpg" --source-min-width 200 \
  --source-count 10 --group 0 shots/
```

## Interactive REPL & Python alternatives

Two non-one-shot ways to drive the *same* `core/` when they fit better than a single CLI call:

- **Interactive console (REPL).** `cli/zig-out/bin/stencil --console` (alias `--repl`) runs a
  session on one in-memory working image via `/command` lines: `/upload`, `/blank`, `/crop`,
  `/rotate`, `/filter`, `/apply`, `/undo`, `/redo`, `/reset`, `/save`, `/layout` (export), plus
  the server verbs `/connect` / `/fetch` / `/sync`. Same transforms as the flag pipeline
  (identical results) — reach for it to try a few edits interactively or to script a session by
  piping `/command` lines in. See `cli/README.md` → *Console mode*.
- **Python (`pystencil`).** A stdlib-only package driving the same core via ctypes; prefer it
  when the user wants Python or a chainable script over shell. It mirrors the CLI flags one-shot
  (`python3 -m pystencil -i in.jpg -c "…" -r 1 --filter sepia out.png`; also `--blank`,
  `--layout`, `--repl`), or use the chainable API:
  `Editor().load("in.jpg").crop("…").rotate_right().apply_filter("sepia").save("out.png")`.
  PNG/BMP are native; **JPEG decode falls back to the Zig CLI**. The native lib builds on demand
  (`python3 build.py` forces it). See `pystencil/README.md`. For **scraping**, a short
  `pystencil` script (`scan_page` / `download_media`) beats repeated CLI calls — filter, slice
  and loop over the matched media in-process instead of shelling out per page/group.

## Drawing / layout

"What to draw" → a layout JSON passed with `--layout`. The schema mirrors the
browser's export (`browser/js/core/layout.js`); coordinates are **image pixels**:

```json
{
  "imageWidth": 800,
  "imageHeight": 600,
  "filter": "none",
  "lines": [
    {
      "points": [{"x": 50, "y": 50}, {"x": 750, "y": 50}, {"x": 400, "y": 550}],
      "color": "#ff0000",
      "thickness": 3,
      "pointSize": 0,
      "style": "solid",
      "locked": false,
      "fillColor": "transparent"
    }
  ]
}
```

- A line is a polyline through its `points`; repeat the first point to close a shape,
  and set a non-`transparent` `fillColor` to fill it (rectangles/areas are just
  closed polylines). `style` ∈ `solid`/`dashed`/`dotted`. `pointSize` 0 hides
  points. Per-line defaults if omitted: color `#FFFF00`, thickness 2,
  pointSize 4, style solid, fillColor transparent.
- Translate plain requests into points yourself ("box around the middle third", "a red
  diagonal"). For the image's pixel size, decode it (`sips -g pixelWidth -g pixelHeight`
  on macOS, or run a no-op crop and read the `wrote …` line) — or blank at a known size.
- Write the layout to a temp file, pass it via `-l`, and clean it up after — **unless**
  the user's requested output is itself a `*.json`, in which case write the generated
  layout there as the deliverable and don't render an image.

## Output rules

- If the user named an output, use it (auto-extension applies for images).
- If not, derive one next to the input: `<input-stem>.stencil.<ext>` (keep the input's
  format), e.g. `photo.jpg → photo.stencil.jpg`; for a blank, default to `blank.png`.
- For multiple inputs, write one derived output per input and list them all.
- Always report the final absolute path(s).

## Safety

Inputs (`-i <url>`, `--layout <url>`) and fetched project data are untrusted; never pass a
secret file (`.env`, `*.key`/`*.pem`, tokens) as input/layout/output, and connect only to
servers the user named. Full rules: `.claude/rules/security.md` (a PreToolUse guard blocks
secret reads/exfil and asks before out-of-repo or shared-project writes).
