# Stencil — CLI (Zig)

A small command-line image tool that wraps Stencil's shared C++ core for quick, headless
image manipulation: load an image, video frame, or blank page, crop / rotate it, draw a
layout, apply a filter, and write the result. For the project overview see the
[repository README](../README.md).

```bash
stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png
stencil --blank 800 600 red --layout notes.json --filter sepia out
stencil -i clip.mp4 -f 24 frame.png
```

## Dependencies

| Purpose | Tool | How it's provided |
|---|---|---|
| Build + language | **Zig 0.16** | the `zig` toolchain |
| Image decode/encode (PNG/JPEG/BMP/TGA) | **stb_image** | public-domain single-header C codecs, fetched on demand (pinned by content hash in `build.zig.zon`) and compiled from source — no native dependency |
| Shared geometry / crop / raster / filter | **`../core/`** | the C++ core, recompiled from source by `build.zig` and called over `../core/cliApi.h` |
| Video frame extraction | **ffmpeg** | optional system tool, shelled out only for video input; if it's not on `PATH` the CLI says so and everything else still works |

Zig ships its own clang, so building needs **no separate C/C++ toolchain**. URL inputs
(`http(s)://`) are fetched with Zig's standard-library HTTP client (`std.http`, native
TLS) — part of Zig, not an added dependency — so image and URL handling need **no system
tools at all**; only video does. The C++ core stays STL-only and codec-free: image codecs,
HTTP, video, and JSON all live in Zig.

## Layout

```
build.zig            # compiles ../core/*.cpp + cliApi.cpp + stb_impl.c, links libc++
build.zig.zon        # package manifest + the pinned stb_image dependency
src/
  main.zig           # entry: logo, parse args, run pipeline
  args.zig           # flag parser (+ --help text)
  logo.zig           # ANSI-coloured console logo (echoes browser/favicon.svg)
  pipeline.zig       # orchestration: source -> crop -> rotate -> layout -> filter -> encode
  console.zig        # interactive --console REPL: input loop + verb/action dispatch
  console/           # the REPL package, driven by console.zig:
    session.zig      #   working image + undo/redo snapshot stack
    commands.zig     #   command grammar (pure parsing: verbs, transforms, /blank, album)
    ui.zig           #   presentation: header, acks, prompt, help, /theme listing
    handlers.zig     #   command implementations over pipeline.zig's steps
  line_edit.zig      # raw-mode line editor: Up/Down history, cursor keys (TTY only)
  theme.zig          # brand-accent palette (mirrors browser/desktop); drives /theme + logo colour
  clipboard.zig      # /paste + /copy clipboard image I/O (macOS via osascript)
  core.zig           # typed wrappers over the C++ core's extern "C" ABI (@cImport)
  image.zig          # stb_image decode/encode (RGBA8 <-> file formats)
  stb_impl.c         # the stb_image / stb_image_write implementation translation unit
  video.zig          # ffmpeg frame grab (to PNG on stdout)
  net.zig            # std.http(s) URL fetch (native TLS, no external tool)
  layout.zig         # std.json -> drawable lines
test_root.zig        # test entry point (inline unit tests + the integration suite)
tests/
  *_test.zig         # integration tests (decode, crop, rotate, format, layout, e2e)
  fixtures/          # sample.png + layout.json used by the tests
```

`src/` is kept flat: it's a small, cohesive set of modules and that's the idiomatic
Zig layout.

> The Zig build recompiles the core sources directly (it does not link the CMake static
> library), so the file list in `build.zig` must stay in sync with `STENCIL_CORE_SOURCES`
> in `../core/CMakeLists.txt`.

## Build

```bash
# from this directory (cli/)
zig build                 # -> zig-out/bin/stencil
zig build run -- --help   # build and run with arguments
```

The first build fetches the `stb` headers (network required once; cached afterwards).

### Docker

A multi-stage [`Dockerfile`](Dockerfile) compiles the CLI (recompiling `core/`) and ships
a slim runtime image with `ffmpeg` for video input. Because `build.zig` pulls in `core/`,
**build from the repo root** and select the Dockerfile with `-f`:

```bash
# from the repo root
docker build -f cli/Dockerfile -t stencil-cli .

# mount a working directory for inputs/outputs (ENTRYPOINT is `stencil`)
docker run --rm -v "$PWD:/work" -w /work stencil-cli -i in.png -r 1 out.png
```

Override the toolchain with `--build-arg ZIG_VERSION=…` (and `ZIG_ARCH=aarch64` on
arm64); for a Zig dev/nightly build, set `--build-arg ZIG_URL=…` to the `ziglang.org/builds/`
tarball. The build fetches the `stb` dependency, so it needs network access.

## Usage

```
stencil [options] <output>
```

| Flag | Description |
|---|---|
| `-i, --input <path\|url>` | Image or video source (file or `http(s)://` URL) |
| `--blank [w h] [color]` | Create a blank page; omit `w h` for the default A4 size, color is a name or `#hex` (default white) |
| `-f, --frame <n>` | Video frame index to grab (default 0) |
| `-c, --crop "<spec>"` | Crop, e.g. `"x1=10% x2=90% y1=10% y2=90%"`. Each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a bare pixel delta. Omit an edge to keep the image bound. |
| `--album` | When only one crop axis is given, derive the other from the page proportion (landscape) |
| `-r, --rotate <int>` | Rotate `int × 90°` (e.g. `-1` = −90°, `3` = 270°) |
| `-l, --layout <path\|url>` | Layout JSON to draw onto the image (same schema the browser exports) |
| `--filter <bw\|sepia\|color>` | Apply an image filter. A colour name/`#hex` makes a duotone tint. **Overrides** the layout's filter if both are present. |
| `--console` | Start [interactive console mode](#console-mode) instead of running a one-shot pipeline. |
| `-h, --help` | Show help |
| `<output>` | Result path. A missing/unknown extension is filled in from the input format (`png`, `jpg`, `bmp`, `tga`). |

`--input` and `--blank` are mutually exclusive. URL inputs are fetched with Zig's built-in
HTTP client (no external tool); video input requires `ffmpeg` on `PATH`, and the CLI exits
with a clear message if it's missing.

### Examples

```bash
# Centre-crop to 80% and rotate a quarter turn clockwise
stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png

# Blank red 800×600, draw a saved layout, tone it sepia (extension auto-filled -> out.png)
stencil --blank 800 600 red --layout notes.json --filter sepia out

# Default-size (A4 @ 96 dpi) blank in a custom colour
stencil --blank "#102030" page.png

# Grab the 24th frame of a video
stencil -i clip.mp4 -f 24 frame.png

# Crop a single axis and derive the other from the page proportion, landscape
stencil -i wide.png -c "x1=0 x2=1200px" --album out.png
```

## Console mode

`stencil --console` (alias `--repl`) skips the one-shot pipeline and instead reads
`/command <args>` lines from stdin, applying each to a single **in-memory working image**.
Every edit is snapshotted, so `/undo`, `/redo` and `/reset` walk a full history. It's handy
for interactively trying a few crops/filters, or for scripting a session by piping commands
in — it reuses the exact same `core/` transforms as the flag pipeline, so results are identical.

```
stencil --console
```

The leading `/` is optional (`crop ...` ≡ `/crop ...`). On a TTY you also get raw-mode line
editing: **Tab** completes the command word, **Up/Down** recall the last 50 commands,
Left/Right/Home/End/Backspace edit the line, and the prompt + leading `/command` token
render in the brand accent. Two clipboard chords work mid-prompt: **Ctrl-V** pastes an image
from the clipboard as the working image, and **Ctrl-C** copies the current image to the
clipboard. To leave, press **Ctrl-C** a second time (or use `/exit` / **Ctrl-D**) — the first
press copies and arms the exit, so any other key cancels it.

| Command | Effect |
|---|---|
| `/upload <path\|url>` | Load an image (or video frame) as the working image (TTY: asks for a yes/no confirmation first). Aliases: `open`, `load`. |
| `/paste` | Load an image from the clipboard (macOS, via `osascript`). |
| `/blank [w h] [color]` | Create a blank page (default A4 @ 96 dpi, white). Alias: `new`. |
| `/apply <file.json>` | Draw a layout JSON onto the image. Aliases: `draw`, `layout`. |
| `/crop <spec> [album]` | Crop, e.g. `x1=10% x2=90% y1=10% y2=90%` (add `album` to derive the missing axis). |
| `/rotate <int>` | Rotate `int × 90°` (e.g. `-1`, `2`, `3`). Aliases: `rot`, `turn`. |
| `/filter <mode>` | `bw` \| `sepia` \| `none` \| a colour name/`#hex` (duotone tint). Shorthands: `/bw`, `/sepia`, `/tint <color>`. |
| `/exec <action> ...` | Run a transform by name (`crop` \| `rotate` \| `filter` \| `apply`). Aliases: `do`, `run`. |
| `/undo` `/redo` | Step back / forward through edits. Aliases: `u`, `r`. |
| `/reset` | Revert to the original, dropping all edits. Alias: `revert`. |
| `/save <path>` | Encode + write the working image to a file (extension filled in if omitted). Alias: `write`. |
| `/copy` | Copy the current image to the clipboard (macOS). Also bound to **Ctrl-V** to paste / **Ctrl-C** to copy. |
| `/status` | Show the working image (path, size, edit position). Aliases: `info`, `image`. |
| `/theme [name\|#hex]` | List the accent colours, or switch: a preset name, `default` (violet), or any colour like `#ff5623`. Also repaints the logo. |
| `/clear` | Clear the screen and redraw the logo + image header. Alias: `cls`. |
| `/drop` | Forget the working image entirely. Aliases: `close`, `forget`. |
| `/help` | List the commands. Aliases: `?`, `h`. |
| `/exit` | Leave console mode. Aliases: `quit`, `q`, **Ctrl-D**, or **Ctrl-C** pressed twice. |

`/apply` is **layout-only** now — crop/rotate/filter are their own commands (or `/exec`). The
image identity is shown as a header just under the logo, refreshed on `/clear`, `/theme` and
after a source change; per-edit feedback is a concise `cropped -> WxH [n/m]` line. A
**URL / blank / clipboard** source lives only in memory: its buffers are freed when another
image is loaded, on `/drop`, or when the session ends. Prompts and messages go to **stderr**
(the CLI's human channel), so a `/save` to stdout-adjacent tooling stays clean. Unknown
commands and failed steps print an error and keep the session running.

```bash
# Script a session by piping commands in (no TTY → plain reader, no line editing)
printf '/upload photo.png\n/crop x1=10%% x2=90%% y1=10%% y2=90%%\n/rotate 1\n/sepia\n/save out.png\n/exit\n' \
  | stencil --console
```

## How it works

```
cli (Zig)  decode file/URL/video-frame ─► RGBA8 buffer
           std.json ─► layout lines / filter
           @cImport(core/cliApi.h) ─► C++ core: crop · rotate · blank · rasterise · filter
           stb_image_write ─► encode ─► output file
```

The C++ core does every pixel/geometry transform (so behaviour matches the browser and
desktop apps by construction); Zig owns all I/O, codecs, video, and JSON. The crop string,
length tokens, album derivation, named-colour parsing, and the line rasteriser are
implemented once in `../core/` and exercised by both this CLI and the core's Doctest suite.

## Test

```bash
zig build test --summary all
```

Two layers run together:

- **Inline unit tests** (in `src/*.zig`) — argument parsing, the C-ABI bridge (colour,
  crop, rotate, fill, filter), codec round-trips, layout JSON parsing, video/URL detection,
  and console-mode command parsing (`parseCommand` / `parseAction` / `parseBlank`).
- **Integration tests** (`tests/*_test.zig`, using `tests/fixtures/`) — decode the PNG
  fixture, crop/rotate it, round-trip every output format, rasterise the layout fixture,
  a full **end-to-end** `pipeline.run` (file in → crop + rotate + layout + filter override
  → file out) that reads the result back and checks its dimensions, and a console-mode
  session driven through `console.handle` (upload → crop → rotate → filter → save → reset).

The core's own geometry/crop/raster logic is additionally covered by its Doctest suite
(`../core`).
