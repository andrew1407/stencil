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

Drive Stencil's headless CLI (`cli/`, a Zig wrapper over the shared C++ `core/`)
to transform an image or a video frame and write the result. This is the simplest
way to "use the editor's core features" without a GUI — every pixel/geometry
operation is the same code the browser and desktop apps run.

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

Run `cli/zig-out/bin/stencil --help` if you need to confirm a flag.

## Action → flag mapping

```
stencil [options] <output>
```

| User asks for | Flag | Notes |
|---|---|---|
| use this image/video | `-i, --input <path\|url>` | local file or `http(s)://` |
| blank canvas | `--blank [w h] [color]` | omit `w h` → A4 @ 96dpi; color name or `#hex` (default white). **Mutually exclusive with `-i`** |
| a frame from a video | `-f, --frame <n>` | 0-based; needs `ffmpeg` on PATH; reads *direct* media, not streaming page URLs |
| crop | `-c, --crop "x1=… x2=… y1=… y2=…"` | each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a bare pixel delta; a leading `-` measures from the far edge; omit an edge to keep the image bound |
| keep page aspect on a 1-axis crop | `--album` | derive the missing axis from the page proportion (landscape) |
| rotate | `-r, --rotate <int>` | **quarter-turns only**: `int × 90°` (`1`=90° CW, `-1`=90° CCW, `2`=180°). Arbitrary angles aren't supported — say so if asked |
| draw something on it | `-l, --layout <path\|url>` | layout JSON (see below) |
| b&w / sepia / tint | `--filter <bw\|sepia\|color>` | a color name or `#hex` makes a duotone tint; **overrides** a filter set inside the layout |
| edit a **server** project | `--server <url>` | with `-i <name>`, `-i` names a project on the [collaboration server](../../../server/README.md) to fetch + edit (not a local path); incompatible with `--blank` |
| save back to that project | `--remote-update` | with `--server`, write the result back into the fetched project |
| publish as a **new** project | `--remote <url>` | upload the result as a new project on a server (any source: local/web `-i`, `--blank`, or a `--server` fetch) |
| name the new project | `--remote-name <name>` | name for `--remote` (default: input image's base name); a web input's URL is recorded as the project source |
| (result file) | `<output>` | positional, last; unknown/missing extension auto-filled from input (`png`/`jpg`/`bmp`/`tga`) |

Order doesn't matter to the CLI; the pipeline always runs
**source → crop → rotate → layout → filter → encode**, then the result is saved locally
**and** delivered to any server (`--remote-update` / `--remote`). `--server` and `--remote`
may point at **different** servers, so one run can fetch a project from one and publish it to
another. (For an interactive multi-server session — `/connect`, `/fetch`, `/sync`, live
update notices — use `--console` mode; see `cli/README.md`.)

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
# publish a local image as a new server project after rotating it
cli/zig-out/bin/stencil -i photo.png -r 1 --remote http://host:8090 --remote-name "Shared" out.png
```

Note: the `/stencil` skill drives the CLI directly; an MCP client can reach the same
server-project actions through the `stencil_edit` tool's `server` / `remote_update` /
`remote` / `remote_name` parameters (see `mcp/README.md`).

## Interactive REPL & Python alternatives

Two non-one-shot ways to drive the *same* `core/`, when they fit the request better than a
single CLI call:

- **Interactive console (REPL).** `cli/zig-out/bin/stencil --console` (alias `--repl`) opens a
  session on one in-memory working image, applying `/command` lines: `/upload`, `/blank`,
  `/crop`, `/rotate`, `/filter`, `/apply`, `/undo`, `/redo`, `/reset`, `/save`, `/layout` (export),
  plus the server verbs `/connect` / `/fetch` / `/sync`. Reach for it to try a few crops/filters
  interactively or to script a session by piping `/command` lines in — same transforms as the
  flag pipeline, so results are identical. See `cli/README.md` → *Console mode*.
- **Python (`pystencil`).** A stdlib-only package that drives the same core via ctypes — prefer
  it when the user wants Python or a chainable script over shell. It mirrors the CLI flags
  one-shot (`python3 -m pystencil -i in.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 --filter sepia
  out.png`; also `--blank`, `--layout`, and `--repl`), or use the chainable API:
  `Editor().load("in.jpg").crop("…").rotate_right().apply_filter("sepia").save("out.png")`.
  No third-party deps; PNG/BMP are native but **JPEG decode falls back to the Zig CLI**. The
  native lib builds on demand (force it with `python3 build.py`). See `pystencil/README.md`.

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
      "markerSize": 0,
      "style": "solid",
      "locked": false,
      "fillColor": "transparent"
    }
  ]
}
```

- A line is a polyline through its `points`; repeat the first point to close a shape,
  and set a non-`transparent` `fillColor` to fill it (rectangles/areas are just
  closed polylines). `style` ∈ `solid`/`dashed`/`dotted`. `markerSize` 0 hides
  point markers. Per-line defaults if omitted: color `#FFFF00`, thickness 2,
  markerSize 4, style solid, fillColor transparent.
- Translate plain requests into points yourself (e.g. "box around the middle third",
  "a red diagonal line", "outline these corners"). When you need the image's pixel
  size first, decode it (`sips -g pixelWidth -g pixelHeight <file>` on macOS, or
  run a no-op crop and read the result) — or generate the blank at a known size.
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
