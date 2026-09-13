# Stencil — CLI (Zig)

A small command-line image tool that wraps Stencil's shared C++ core for quick, headless
image manipulation: load an image, video frame, or blank page, crop / rotate it, draw a
layout, apply a filter, and write the result. For the project overview see the
[repository README](../README.md); for how the tool is built, [`ARCHITECTURE.md`](ARCHITECTURE.md).

```bash
stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png
stencil --blank 800 600 red --layout notes.json --filter sepia out
stencil -i clip.mp4 -f 24 frame.png
```

## Build

Needs **Zig 0.16**; Zig ships its own clang, so no separate C/C++ toolchain. Video input
additionally needs `ffmpeg` on `PATH` (optional — everything else works without it).

```bash
# from this directory (cli/)
zig build                 # -> zig-out/bin/stencil
zig build run -- --help   # build and run with arguments
zig build test --summary all
zig build bench           # opt-in benchmarks
```

`scripts/tui_smoke.py` is a manual smoke check for the full-screen TUI in a pseudo-terminal.

The first build fetches the `stb` image headers (network required once; cached afterwards).

### Docker

The multi-stage [`Dockerfile`](Dockerfile) compiles the CLI and ships a slim runtime image
with `ffmpeg`. Build **from the repo root** (it pulls in `core/`):

```bash
docker build -f cli/Dockerfile -t stencil-cli .
docker run --rm -v "$PWD:/work" -w /work stencil-cli -i in.png -r 1 out.png
```

Override the toolchain with `--build-arg ZIG_VERSION=…` (and `ZIG_ARCH=aarch64` on arm64);
for a Zig nightly, `--build-arg ZIG_URL=…` to the `ziglang.org/builds/` tarball.

## Usage

```
stencil [options] <output>
```

| Flag | Description |
|---|---|
| `-i, --input <path\|url>` | Image or video source (file or `http(s)://` URL) |
| `--blank [format] [w h] [color]` | Create a blank page; a leading page-format name (`a0`…`c10`, case-insensitive) **or** explicit `w h` picks the size (omit both for A4), color is a name or `#hex` (default white) |
| `-f, --frame <n>` | Video frame index to grab (default 0) |
| `-c, --crop "<spec>"` | Crop, e.g. `"x1=10% x2=90% y1=10% y2=90%"`. Each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a bare pixel delta. Omit an edge to keep the image bound. |
| `--album` | When only one crop axis is given, derive the other from the page proportion (landscape) |
| `-r, --rotate <int>` | Rotate `int × 90°` (e.g. `-1` = −90°, `3` = 270°) |
| `-l, --layout <path\|url>` | Layout JSON to draw onto the image (same schema the browser exports) |
| `--filter <bw\|sepia\|invert\|contour\|color>` | Apply an image filter (`invert` = negative, `contour` = edge detection). A colour name/`#hex` makes a duotone tint. Overrides the layout's filter if both are present. |
| `--source-site <url>` | **Scrape mode:** fetch a page, extract + filter its media, and download the matches into `<output>` (a **directory**). See [Scraping a page](#scraping-a-page). Mutually exclusive with `-i`, `--blank`, `--server`. |
| `--source-count <n>` | Items per page/group in scrape mode (default `5`; `0` = all matches). |
| `--group <g>` | 0-based page index over the filtered list (default 0). |
| `--source-filter <s>` | Category tokens, `\|`-joined: `img` \| `video` \| `background` \| `poster` (default `all`). |
| `--source-format <s>` | Format tokens, `\|`-joined, e.g. `png\|jpg\|webp\|mp4` (default `all`). |
| `--source-name <regex>` | POSIX ERE matched against each media URL (max 200 characters). |
| `--source-min-width` / `--source-max-width` / `--source-min-height` / `--source-max-height` `<px>` | Inclusive pixel bounds (`0` = unset); images are measured from a header sniff, unmeasured items pass. |
| `--console` | Start [interactive console mode](#console-mode) instead of running a one-shot pipeline. |
| `--console-full-screen` | Console in a full-screen TUI (pinned logo header, scrollback, mouse). Implies `--console`. |
| `--server <url>` | Connect to a [collaboration server](../server/README.md); then `-i <name>` names a **server project** to fetch and edit. |
| `--remote-update` | With `--server`, write the result back into the fetched server project. |
| `--remote <url>` | Upload the result as a **new** project on a server (for a local/web input). |
| `--remote-name <name>` | Name for the `--remote` project (default: the input image's base name). |
| `--token <tok>` | Access token for `--server` / `--remote` — needed when the server gates token minting (`ADMIN_TOKEN`). Takes a session token or the admin token itself. |
| `--confine-output` | Refuse an output path that leaves the working directory (absolute or `~`-prefixed; `..` traversal is always refused). Off by default; the mcp and bot adapters pass it when forwarding model-chosen paths. |
| `-h, --help` | Show help |
| `<output>` | Result path. A missing/unknown extension is filled in from the input format (`png`, `jpg`, `bmp`, `tga`). |

`--input` and `--blank` are mutually exclusive.

### Examples

```bash
# Centre-crop to 80% and rotate a quarter turn clockwise
stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png

# Blank red 800×600, draw a saved layout, tone it sepia (extension auto-filled -> out.png)
stencil --blank 800 600 red --layout notes.json --filter sepia out

# Blank on a named page format (B5 @ 96 dpi), pink
stencil --blank b5 pink page.png

# Grab the 24th frame of a video
stencil -i clip.mp4 -f 24 frame.png

# Crop a single axis and derive the other from the page proportion, landscape
stencil -i wide.png -c "x1=0 x2=1200px" --album out.png

# Upload a local image as a new shared project, after rotating it
stencil -i photo.png -r 1 --remote http://host:8090 --remote-name "Shared" out.png

# Fetch a server project by name, apply a filter, and write the result back
stencil --server http://host:8090 -i Shared --filter sepia --remote-update out.png
```

### Project files (`.stencil`)

A `.stencil` file bundles a whole project — the original image, the layout
(crop/rotation/filter/lines) and metadata — and opens in every Stencil surface. The CLI
reads and writes it on either side of a one-shot, and via `/open` / `/save` in `--console`:

```bash
stencil -i project.stencil out.png                                  # render a project to PNG
stencil -i photo.jpg -c "x1=10% x2=90%" -r 1 --filter sepia out.stencil   # bundle an image + edits
```

### Scraping a page

`--source-site <url>` fetches the page over the same guarded HTTP client as image URLs and
hand-parses the HTML — `<img>` (with lazy-attr / `srcset` fallbacks), inline `<svg><image>`,
`<video>` + its `poster`, `<picture><source>`, and CSS `background-image` from inline
`style=` and `<style>` blocks — then filters the matches **category → format → dimension**
and downloads them into `<output>` (a directory, created if missing). Results are paged by
`--source-count` and `--group`.

Output goes to stderr, one line per file plus a summary; a per-item fetch failure prints
`error: could not fetch {url} ({reason})` and the run continues; zero matches is a hard error.

```bash
# Download every image at least 200px wide into ./shots/
stencil --source-site https://example.com --source-filter img --source-min-width 200 shots/

# Just the PNGs and JPEGs, first page of 10
stencil --source-site https://example.com --source-format png|jpg --source-count 10 out/
```

### Running a script

A `.stc` script is a list of `@` directives: one file that opens sources, edits them and
saves the results. `--script` runs one, `--script-check` only reports what is wrong with it.

```stc
# shots.stc — every PNG in the folder, cropped and toned
@source shots/:
    @use line red dashed, 3px, point blue 5px
    @rect (10, 10) (-10%, -10%)
    @crop 5%
    @filter sepia
    @save out/
```

```bash
stencil --script shots.stc              # run it
stencil --script-check shots.stc        # just the diagnostics, one line each
stencil -i photo.png --script marks.stc # a script with no @source edits this image
cat shots.stc | stencil --script -      # '-' reads the script from stdin
```

A bare `@save` writes beside the source as `<name>-stencil.<ext>`, so a whole-directory run
is safe in place. A script with any error runs nothing and exits 1.

The language — units, colours, templates, undo — is written out in
[`stc-contract/stc-contract.md`](../stc-contract/stc-contract.md), and the worked examples
are the `tour-*.stc` files in `browser/js/config/script/fixtures/`.

## Console mode

`stencil --console` (alias `--repl`) reads `/command <args>` lines from stdin and applies
each to a single in-memory working image; every edit is snapshotted, so `/undo`, `/redo`
and `/reset` walk a full history. The leading `/` is optional. Pipe commands in to script a
session:

```bash
printf '/upload photo.png\n/crop x1=10%% x2=90%% y1=10%% y2=90%%\n/rotate 1\n/sepia\n/save out.png\n/exit\n' \
  | stencil --console
```

On a TTY the line is editable: **Tab** completes the command word, **Up/Down** recall
history, **Ctrl-A/E** jump to line start/end, **Alt+←/→** move by word, **Ctrl-W** /
**Alt-Backspace** delete the word before the cursor, **Ctrl-U** clears the line. **Ctrl-V**
pastes the clipboard onto the line — a picture attaches as an `[Image #n]` marker (up to 3 on
a `/prompt`, 1 on a `/upload`) and is loaded when you press Enter; **Ctrl-Z** removes the
last one; **Ctrl-Alt-C** copies the current image out. **Ctrl-C** cancels a running
`/prompt`; pressed twice it exits (as does `/exit` or **Ctrl-D**).

> **macOS:** Terminal.app swallows Option+arrow by default — enable *Use Option as Meta key*
> in Settings → Profiles → Keyboard; in iTerm2 pick the *Natural Text Editing* preset. The
> Ctrl chords need no configuration.

`--console-full-screen` runs the console as a full-screen TUI: the logo is pinned as a
header, output scrolls beneath it (**mouse wheel**, **PgUp/PgDn**), a click on the logo
cycles the accent colour and a double-click picks a random one. Drag to select text and
press **Ctrl-S** (or **Ctrl-C**) to copy it; hold **Shift** while dragging for the terminal's
own selection, or `/mouse off` to hand the mouse back entirely
(`STENCIL_CONSOLE_MOUSE=on|off` sets the default). `/reveal-speed <0.01–1>` sets how fast new
output is revealed (`1` = instantly; `STENCIL_CONSOLE_REVEAL_SPEED` sets the default). A
terminal too short to fit falls back to the plain editor; `NO_COLOR` keeps full-screen but
monochrome.

| Command | Effect |
|---|---|
| `/upload <path\|url>` | Load an image (or video frame) as the working image (TTY: asks first). Bare `/upload` loads the clipboard's picture. Each upload also joins the current turn's attachments for the next `/prompt`. Aliases: `open`, `load`. |
| `/source-upload <url> [index] [format] [name=<label>] [minW] [maxW] [minH] [maxH]` | Scrape a page and load its *index*-th image-category match as the working image (`-1` = unset bound). Alias: `scrape`. |
| `/paste` | Load an image from the clipboard (macOS `osascript`, Linux `wl-paste`/`xclip`, Windows PowerShell): a PNG, a TIFF, or an image file copied in a file manager. |
| `/unpaste [n]` | Take back an image added this turn (bare = the newest). Also **Ctrl-Z**. Alias: `pop`. |
| `/images` | List the images this turn will send to the assistant, with the working image marked. |
| `/blank [format] [w h] [color]` | Create a blank page (default: the picked `/format`, else A4 @ 96 dpi, white). Alias: `new`. |
| `/format [name\|custom w h]` | Bare lists every page format with its cm size; a name picks the session's page format, `custom <w> <h>` sets explicit cm dims. Alias: `formats`. |
| `/apply <file.json>` | Draw a layout JSON onto the image. Alias: `draw`. |
| `/crop <spec> [album]` | Crop, e.g. `x1=10% x2=90% y1=10% y2=90%`. Bare prints the spec vocabulary. |
| `/rotate <int>` | Rotate `int × 90°`. Aliases: `rot`, `turn`. |
| `/filter <mode>` | `bw` \| `sepia` \| `invert` \| `contour` \| `none` \| a colour (duotone tint). Shorthands: `/bw`, `/sepia`, `/invert`, `/contour`, `/tint <color>`. |
| `/exec <action> ...` | Run a transform by name (`crop` \| `rotate` \| `filter` \| `apply`). Aliases: `do`, `run`. |
| `/undo` `/redo` | Step back / forward through edits. Aliases: `u`, `r`. |
| `/reset` | Revert to the original, dropping all edits. Alias: `revert`. |
| `/save [path]` | Write the working image to a file (`~` expanded; only `..` traversal is refused). Bare `/save` pushes the current result to the active server project. Alias: `write`. |
| `/layout [path]` | Export the current layout JSON. A `.json` path is written verbatim, another path is a directory (`<path>/<project>.json`), bare writes `<project>.json` in the cwd. |
| `/open <file.stencil>` · `/save <file.stencil>` · `/delete <file.stencil>` | Open, save and delete a local `.stencil` project file. |
| `/connect <url [token][ url2 …]>` | Connect to one or more [collaboration servers](../server/README.md). A token word authenticates against an admin-gated server; an invite link (`<url>#token=<tok>`) works as the URL on its own. |
| `/connections [admin\|session]` | List the connected servers with a live reachability status; admin-credential rows are tagged `[admin]`. Alias: `servers`. |
| `/projects [url]` | List a server's projects (every connected server when no URL). Alias: `ls`. |
| `/disconnect [url]` · `/reconnect [url]` | Close one connection (or the latest) · re-establish one (or all), re-issuing the token and reviving the live feed. |
| `/fetch <name> [url]` | Load a server project's image to keep editing. Bare lists what there is to fetch. Alias: `pull`. |
| `/sync [on\|off]` | Live-editing mode for the active fetched project: your edits auto-upload (debounced) and a peer's saves auto-pull over the server's raw-TCP edit channel (skipped for `https://` servers). Local unsynced edits are never clobbered — you get a note to `/save` or `/fetch`. |
| `/prompt <text>` | Ask the configured LLM assistant to plan edits ([`llm-contract.md`](../llm-contract/llm-contract.md)): the working image is attached for vision (≤ 8 MiB), the reply is printed, and the validated op-plan runs through the same session operations as the commands above. Variants render as `variant-<label>.png`. Every `/upload` since the last prompt is an attachment of the turn. Alias: `p`. |
| `/llm [provider\|url\|model\|key\|server <value>]` | Show (bare, secrets masked) or override the session's LLM config: `ollama` \| `openai-compat` \| `stencil-server`. Initial values come from the `STENCIL_LLM_*` env keys. |
| `/chat [on\|off\|clear]` | Chat persistence (default off): save the conversation with the project. |
| `/copy` | Copy the current image to the clipboard. Also **Ctrl-Alt-C**. |
| `/status` | Show the working image (path, size, edit position). Aliases: `info`, `image`. |
| `/project-color [#hex\|name\|clear]` | Get or set the active fetched project's accent colour (pushed to the server). |
| `/theme [name\|#hex]` | List the accent colours, or switch to one. |
| `/mouse [on\|off]` · `/reveal-speed [0.01-1]` | Full-screen only: mouse reporting · output reveal speed. |
| `/clear` | Clear the scrollback / screen. Alias: `cls`. |
| `/drop` | Forget the working image entirely. Aliases: `close`, `forget`. |
| `/help` | List the commands. Aliases: `?`, `h`. |
| `/exit` | Leave console mode. Aliases: `quit`, `q`, **Ctrl-D**, **Ctrl-C** twice. |

A bare command that needs arguments lists its options instead of failing. Prompts and
messages go to **stderr**, so a `/save` to stdout-adjacent tooling stays clean. Before
`/prompt` can do anything you need a model behind it — see the
[root README](../README.md#ai-assistant--setting-up-a-model).
