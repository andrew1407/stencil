# Stencil — CLI (Zig)

A small command-line image tool that wraps Stencil's shared C++ core for quick, headless
image manipulation: load an image, video frame, or blank page, crop / rotate it, draw a
layout, apply a filter, and write the result. For the project overview see the
[repository README](../README.md); for how the tool is built, [`ARCHITECTURE.md`](ARCHITECTURE.md);
for what the console looks like, [`usecases/docs/cli/USECASES.md`](../usecases/docs/cli/USECASES.md).

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
zig build test --summary all   # also fails on a file `zig fmt` would change
zig build fmt             # just that check; `zig fmt .` fixes it
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

### Release packaging

Zig cross-compiles the core and the codecs, so one machine produces every platform's binary:

```bash
# from this directory (cli/)
zig build -Dtarget=x86_64-linux-musl -Doptimize=ReleaseFast -Dstrip=true   # -> zig-out/bin/stencil
```

| Platform | Package | Form |
|---|---|---|
| Linux | `stencil-cli-<ver>-Linux-<arch>.tar.gz` | static `stencil` (musl, any distro) |
| macOS | `stencil-cli-<ver>-Darwin-<arch>.tar.gz` | `stencil` |
| Windows | `stencil-cli-<ver>-Windows-AMD64.zip` | `stencil.exe` |

The macOS binary is unsigned: clear the quarantine flag after downloading (`xattr -d
com.apple.quarantine stencil`). The Windows build runs every one-shot mode (`-i`, `--blank`,
`--script`, `--source-site`, `--server`) but not the interactive console. CI builds all of them
on every `v*` tag and attaches them to the GitHub release
(`.github/workflows/cli-packages.yml`); a manual run produces them as workflow artifacts.

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
| `--thumbnail <px>` | Shrink the result so its longer side is at most `<px>` pixels — area-averaged, aspect kept, never enlarged. The last step before the file is written; the `wrote` line reports the shrunk size. One-shot image runs only. |
| `--source-site <url>` | **Scrape mode:** fetch a page, extract + filter its media, and download the matches into `<output>` (a **directory**). See [Scraping a page](#scraping-a-page). Mutually exclusive with `-i`, `--blank`, `--server`. |
| `--source-count <n>` | Items per page/group in scrape mode (default `5`; `0` = all matches). |
| `--group <g>` | 0-based page index over the filtered list (default 0). |
| `--source-filter <s>` | Category tokens, `\|`-joined: `img` \| `video` \| `background` \| `poster` (default `all`). |
| `--source-format <s>` | Format tokens, `\|`-joined, e.g. `png\|jpg\|webp\|mp4` (default `all`). |
| `--source-name <regex>` | POSIX ERE matched against each media URL (max 200 characters). |
| `--source-min-width` / `--source-max-width` / `--source-min-height` / `--source-max-height` `<px>` | Inclusive pixel bounds (`0` = unset); images are measured from a header sniff, unmeasured items pass. |
| `--prompt "<text>"` | Ask the configured model to plan edits on the input, run the plan and write `<output>` (an output path is required). The model comes from the `STENCIL_LLM_*` environment — see [Using your own Claude key](#using-your-own-claude-key). Exits 1 when the model gives no usable answer. Not available on Windows, like the console. |
| `--console` | Start [interactive console mode](#console-mode) instead of running a one-shot pipeline. |
| `--console-full-screen` | Console in a full-screen TUI (pinned logo header, scrollback, mouse). Implies `--console`. |
| `--server <url>` | Connect to a [collaboration server](../server/README.md); then `-i <name>` names a **server project** to fetch and edit. |
| `--remote-update` | With `--server`, write the result back into the fetched server project. |
| `--remote <url>` | Upload the result as a **new** project on a server (for a local/web input). |
| `--remote-name <name>` | Name for the `--remote` project (default: the input image's base name). |
| `--token <tok>` | Access token for `--server` / `--remote` — needed when the server gates token minting (`ADMIN_TOKEN`). Takes a session token or the admin token itself. Without it, see [Server tokens from the environment](#server-tokens-from-the-environment). |
| `--list-projects` | With `--server`, print every project's metadata as JSON on stdout. |
| `--project-info <id\|name>` | With `--server`, print one project's metadata as JSON on stdout. |
| `--project-update <id>` | With `--server`, change a project's metadata and print it as JSON: give one or more of `--set-name`, `--set-description`, `--set-keywords <a,b>`, `--set-color`, `--set-blank-color` (`''` clears) and `--set-expires <epoch ms>` (`0` = never). `--if-version <n>` refuses the change once a peer has saved past version `n`. |
| `--project-files <id>` | With `--server`, print a project's metadata and the files it stores (`original`, `result`, `video`, `chat`, `variant1`…`variant8`), each with its format. |
| `--project-file <id> <kind>` | With `--server`, download that stored file to `<output>` exactly as served, and print what was written as JSON. |
| `--probe` | Print the `-i` input's format, pixel size, alpha and byte size as JSON on stdout, read from its header; a video adds `durationMs` and `frames` (needs ffprobe). |
| `--no-clobber` | Refuse to overwrite an existing output — checked on the name it would really get, extension filled in. |
| `--confine-output` | Refuse an output path that leaves the working directory (absolute, `~`-prefixed, or out through a symbolic link; `..` traversal is always refused). Off by default; the mcp and bot adapters pass it when forwarding model-chosen paths. |
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

# List a server's projects, or read one, as JSON
stencil --server https://team.example --list-projects
stencil --server https://team.example --project-info Shared

# Rename a server project and tag it, then download its rendered result
stencil --server https://team.example --project-update p_1_a --set-name "Plans v2" --set-keywords floor,draft
stencil --server https://team.example --project-file p_1_a result plans.png

# Size, format and alpha of an image (or a video's length) without decoding it
stencil --probe -i photo.jpg

# Never overwrite: fails if out.jpg already exists
stencil -i photo.jpg --filter bw --no-clobber out

# A 256 px preview of the edited result (longer side), whatever the source size
stencil -i photo.jpg -r 1 --thumbnail 256 preview.png

# Let the configured model plan the edit (see "Using your own Claude key")
stencil -i photo.jpg --prompt "rotate it right and tone it sepia" out.png
```

### Server tokens from the environment

Instead of `--token`, a server's token can come from the environment, so it never shows in a
process listing:

```bash
export STENCIL_SERVER_TOKENS="http://localhost:8090=local-token,https://team.example=team-token"
stencil --server http://localhost:8090 -i Plans --remote https://team.example out.png
```

`STENCIL_SERVER_TOKENS` is a comma list of `origin=token` pairs; each server URL uses the
token of its own origin (scheme, host and port), so `--server` and `--remote` can use
different ones. `STENCIL_SERVER_TOKEN` is the one token for any server the list does not
name. `--token`, or a `#token=` invite link, still wins over both. In `--console`,
`/connect <url>` without a token reads them the same way.

### Using your own Claude key

The CLI can talk to Anthropic's Claude API directly with your own API key — no Stencil server
in between. The key is held only in the memory of the command or console that took it, for at
most 12 hours, and is never written to a file, the console history or the scrollback.

```bash
export STENCIL_LLM_PROVIDER=anthropic
read -rs STENCIL_LLM_API_KEY && export STENCIL_LLM_API_KEY    # paste the key; nothing is shown
stencil -i photo.jpg --prompt "crop 10% off the left and make it b&w" out.png
```

`STENCIL_LLM_MODEL` picks the model (empty = the default Claude model) and
`STENCIL_LLM_BASE_URL` another endpoint (default `https://api.anthropic.com`). The key is only
sent over `https`, or over plain `http` to `localhost`.

In the console, `/llm provider anthropic` switches to Claude and `/llm key` asks for the key
with the input hidden (paste it and press Enter). `/llm` shows until when it is held;
`/llm key forget` drops it at once, and it is gone when the console exits. Once it has expired,
the next `/prompt` asks for it again. A key typed as `/llm key <key>` also works, but it shows
on screen while you type; the console and its history keep only `/llm key`. A key never
follows a switch to or from `anthropic`.

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
saves the results. `--script` runs one, `--script-check` only reports what is wrong with it,
and `--script-plan` prints what it would do without doing any of it.

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
stencil --script-plan shots.stc         # what it would do, as JSON on stdout
stencil -i photo.png --script marks.stc # a script with no @source edits this image
cat shots.stc | stencil --script -      # '-' reads the script from stdin
```

A bare `@save` writes beside the source as `<name>-stencil.<ext>`, so a whole-directory run
is safe in place; when the source is a URL it lands in the working directory under that name.
A script with any error runs nothing and exits 1.

### Emitting it for another surface

`--script-emit <out>` writes the script out as a runnable script somewhere else instead of
running it. The output's own extension picks the target, so there is no language flag:

```bash
stencil --script shots.stc --script-emit shots.pystc   # python, over pystencil's Editor
stencil --script shots.stc --script-emit shots.stcjs   # javascript, over window.stencil
```

`.js` and `.stcjs` emit the browser facade; `.py` and `.pystc` emit pystencil; any other
suffix is an error. Lengths (`10%`), `@source` specs and `@save` targets come out **as
written** and are resolved by the generated file when it runs, so it stays as general as the
`.stc` — a `@source shots/:` block becomes the same loop over the same files. Templates
arrive expanded and `@undo` already reconciled, because the core resolves both when it lowers
the script. Where a target cannot honour a directive it says so and writes nothing: the
browser has no filesystem, so a local `@source` is refused there, and `@frame` needs the
decoder only the CLI has.

`--script-plan` writes one JSON object to stdout — the script's blocks, the files each one
would open, the edits as op-plan actions and the exact paths each `@save` would write — and
touches nothing. Its shape is pinned in [`CONTRACT.md`](CONTRACT.md) §4.3.

The language — units, colours, templates, undo — is written out in
[`contracts/stc/stc-contract.md`](../contracts/stc/stc-contract.md), and the worked examples
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
| `/prompt <text>` | Ask the configured LLM assistant to plan edits ([`llm-contract.md`](../contracts/llm/llm-contract.md)): the working image is attached for vision (≤ 8 MiB), the reply is printed, and the validated op-plan runs through the same session operations as the commands above. Variants render as `variant-<label>.png`. Every `/upload` since the last prompt is an attachment of the turn. Alias: `p`. |
| `/llm [provider\|url\|model\|key\|server <value>]` | Show (bare, secrets masked) or override the session's LLM config: `ollama` \| `openai-compat` \| `anthropic` \| `stencil-server`. Initial values come from the `STENCIL_LLM_*` env keys. Bare `/llm key` asks for the key with the input hidden; `/llm key forget` drops it. See [Using your own Claude key](#using-your-own-claude-key). |
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
