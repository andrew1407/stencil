---
name: stencil
description: >-
  Edit an image (or a video frame) headlessly using Stencil's own core, then
  save the result. Use when asked to crop, rotate (quarter-turns), tint/filter
  (b&w / sepia / duotone color), draw a layout over, extract a video frame from,
  or create a blank page — for one or more local files or http(s) URLs, or a whole
  folder of them through a `.stc` script. Also use when the user invokes /stencil
  explicitly. Drives the Zig CLI (cli/), which
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
   (the first build fetches the `stb` headers; where the SDK is newer than Zig supports —
   `INFINITY` undeclared — use the `ZIG_LIBC` recipe in `CLAUDE.md`). If `zig` isn't on
   `PATH`, tell the user — offer the Docker path (`docker build -f cli/Dockerfile
   -t stencil-cli . && docker run --rm -v "$PWD:/work" -w /work stencil-cli …`,
   built from the repo root) — and stop.
3. **Translate the request into a CLI call** (mapping below) — one per input, or, for
   the same edits over several files, **one `.stc` script** (see *Scripts*) — run it,
   then report the absolute output path(s) and what was applied. Don't overwrite an
   existing file the user didn't name as the output without confirming; `--no-clobber`
   makes the CLI refuse it on the name the result really lands under.

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
  a leading `~`, or out through a symbolic link; `..` is refused either way). Off by default. Pass it when the path
  came from somewhere other than the user — that's why the mcp and bot adapters set it.

### Server projects

`--server <url>` makes `-i <name>` fetch a **project** from a [collaboration
server](../../../server/README.md) instead of a local path (so not with `--blank`);
`--remote-update` writes the result back into it — a shared project others may be editing, so
confirm first; `--remote <url>` (+ `--remote-name`) publishes the result as a **new** project.
`--server` and `--remote` may be different servers. Tokens come from `--token`, or off argv from
`STENCIL_SERVER_TOKEN` / `STENCIL_SERVER_TOKENS` (`origin=token` pairs). `--list-projects` and
`--project-info <id|name>` print JSON. The result is always saved locally too; for a live
multi-server session use `--console` (`cli/README.md`).

### Examples
```bash
# center-crop to 80% and rotate a quarter-turn clockwise
cli/zig-out/bin/stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png
# blank red 800x600, draw a saved layout, tone it sepia
cli/zig-out/bin/stencil --blank 800 600 red --layout notes.json --filter sepia out
# grab the 24th frame of a video as a still
cli/zig-out/bin/stencil -i clip.mp4 -f 24 frame.png
# duotone tint an image fetched from a URL
cli/zig-out/bin/stencil -i https://example.com/pic.png --filter "#7c3aed" tinted.png
# fetch server project "Shared", tone it sepia, write the result back to it
cli/zig-out/bin/stencil --server http://host:8090 -i Shared --filter sepia --remote-update out.png
```

## Scripts (`.stc`) — batches and reusable recipes

One `.stc` file of `@` directives beats N calls: a `@source <file|url|dir|glob>:` block runs
once per matched file, its indented body any of `@crop` / `@filter` / `@line` / `@rect` /
`@layout` / `@frame` / `@undo` / `@save`, in `px % cm mm in` units. The language is
`contracts/stc/stc-contract.md`; the worked examples are the `tour-*` cases in
`browser/js/config/script/fixtures/cases.txt` — read those rather than writing from memory.

```bash
cat > batch.stc <<'STC'
@source shots/*.jpg:
    @crop x1=10% x2=-10%
    @filter sepia
    @save
STC
cli/zig-out/bin/stencil --script-check batch.stc   # diagnostics on stdout; exit 1 on an error
cli/zig-out/bin/stencil --script batch.stc         # a script with any error runs nothing
```

- **Check first.** `--script-check <file>` prints `file:line:col: error|warning: … [CODE]` and
  runs nothing; `--script-plan <file>` prints the lowered op plan as one JSON object. Both
  read stdin for `-`.
- A bare `@save` writes beside its source with a `-stencil` suffix, so a folder script is safe
  to run in place; every `@save` prints the usual `wrote …` line. `--confine-output` applies to
  each `@save` — pass it when the script's save targets did not come from the user.
- `--script <in.stc> --script-emit <out>` writes the script as a runnable program instead of
  running it; `out`'s extension picks the target — `.js`/`.stcjs` for the browser's
  `window.stencil`, `.py`/`.pystc` for pystencil.

## Scrape a source site (headless media download)

For "grab the images/videos off this page", `--source-site <url>` switches the CLI into **scrape
mode**: it fetches and parses the page (no browser), filters the media it finds and downloads
the matches — the positional `<output>` is a **destination folder** (default `.`), not an image.
Scrape mode ignores the editing and server flags and excludes `-i` / `--blank`. It keeps
`--source-count <N>` items (**default 5**, `0` = all) of page `--group <G>`, narrowed by
`--source-filter`, `--source-format`, `--source-name <regex>` and the `--source-min/max-width/height`
bounds (`--help` has the details). Each file prints `wrote …`, the run ends with
`scraped {n} file(s) from {host} into {dir}`, and zero matches is an error.

```bash
# download the first 10 PNG/JPG images at least 200px wide into ./shots/
cli/zig-out/bin/stencil --source-site https://example.com \
  --source-filter img --source-format "png|jpg" --source-min-width 200 \
  --source-count 10 --group 0 shots/
```

## Interactive REPL & Python alternatives

- **Console.** `cli/zig-out/bin/stencil --console` (alias `--repl`) keeps one in-memory working
  image and takes `/command` lines (`/help` lists them, the server verbs included) — for trying
  edits interactively or piping a session in. See `cli/README.md` → *Console mode*.
- **Python (`pystencil`)**, stdlib-only over the same core, when the user wants Python: the CLI's
  flags one-shot (`python3 -m pystencil -i in.jpg -c "…" -r 1 --filter sepia out.png`) or the
  chainable `Editor().load("in.jpg").crop("…").rotate_right().apply_filter("sepia").save("out.png")`.
  For scraping filtered and looped in-process, `scan_page` / `download_media` beat repeated
  CLI calls. See `pystencil/README.md`.

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
  diagonal"). For the image's pixel size, `cli/zig-out/bin/stencil --probe -i <path|url>`
  prints `{"format","width","height","alpha","bytes"}` as JSON on stdout (a video adds
  `durationMs` and `frames`) — or blank at a known size.
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
secret reads/exfil and asks before an out-of-repo write or a `--remote-update`).
