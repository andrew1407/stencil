# Stencil — MCP server (Rust)

A Model Context Protocol (MCP) server that exposes Stencil's image/video editing pipeline as
tools any MCP client (Claude Code, Claude Desktop, or your own agent) can call: load an
image, video frame, or blank page, crop / rotate it, draw a layout, apply a filter, and write
the result — read an image's dimensions, scrape a web page's media, or fetch, edit and
publish projects on a Stencil [collaboration server](../server/README.md). It shells out to
the Zig CLI, so results match the other editors. For the project overview see the
[repository README](../README.md); for how the server is built,
[`ARCHITECTURE.md`](ARCHITECTURE.md).

```jsonc
// register with an MCP client (e.g. Claude Desktop's mcpServers, or `claude mcp add`):
{
  "mcpServers": {
    "stencil": {
      "command": "/path/to/stencil-mcp",
      // optional defaults (see Configuration):
      "env": {
        "STENCIL_SURFACES": "cli,browser",
        "STENCIL_BROWSER_URL": "http://localhost:8080"
      }
    }
  }
}
```

## Build

Needs a stable Rust toolchain and, at runtime, the Stencil CLI (`zig build` in
[`../cli`](../cli), or `STENCIL_CLI` pointing at a binary).

```bash
# from this directory (mcp/)
cargo build              # -> target/debug/stencil-mcp
cargo build --release    # -> target/release/stencil-mcp
cargo test               # the e2e cases self-skip without the CLI binary
cargo test -- --ignored  # the opt-in timing suite
```

### Docker

The multi-stage [`Dockerfile`](Dockerfile) builds both the Zig CLI and the Rust server and
ships a slim runtime with `ffmpeg` and `STENCIL_CLI` pre-wired. Build **from the repo root**
(it pulls in `core/` and `cli/`):

```bash
docker build -f mcp/Dockerfile -t stencil-mcp .
docker run --rm -i -v "$PWD:/work" -w /work stencil-mcp   # MCP over stdio: run with -i
```

## Configuration

The CLI always does the pixel work; **surfaces** decide where each result is delivered. The
default surface(s) are configured once, and any tool call can override them with a `surface`
parameter.

| Surface | Delivery | Needs |
|---|---|---|
| `cli` | writes the output file (always implied) | the Stencil CLI |
| `desktop` | also launches the Qt app showing the result (`stencil --src`) | a built `desktop/` binary |
| `browser` | also builds (and optionally opens) an editor launch URL with the result loaded | the `browser/` app served |
| `browser-live` | **delegated** to the [`stencil-operator` agent](../.claude/agents/stencil-operator.md) for live tab driving; still returns a launch URL | Chrome + the served app |
| `extension` | **delegated** to the `stencil-operator` agent for page scanning; still returns the edited file + URL | Chrome + the extension |

Configuration is read from **built-in defaults ← a `.env` file ← process env (the
`mcpServers` `"env"`) ← the `--surface` arg**, and a per-call `surface` parameter overrides
the resolved default. Copy [`.env.example`](.env.example) to `.env` (gitignored) to set
local values:

| Variable | Default | Meaning |
|---|---|---|
| `STENCIL_SURFACES` | `cli` | default surfaces as a comma list, e.g. `cli,desktop,browser` (`cli` is always included) |
| `STENCIL_CLI` | auto-discovered | path to the `stencil` CLI binary (`STENCIL_CLI` → the repo's `cli/zig-out/bin/stencil` → `PATH`) |
| `STENCIL_CLI_TIMEOUT_SECONDS` | `120` | per-invocation deadline; a CLI run past it is killed and reported as an `error:` |
| `STENCIL_DESKTOP` | `<repo>/desktop/build/stencil` | path to the Qt desktop binary |
| `STENCIL_BROWSER_URL` | `http://localhost:8080` | base URL of the served editor — only for the browser surfaces, only if not on the default |
| `STENCIL_AUTO_OPEN` | `false` | open the browser URL with the OS opener (`open`/`xdg-open`) |

The per-call `surface` parameter accepts a JSON array (`["cli","browser"]`) or a single
string (`"browser"`).

**LLM assistant** (`stencil_prompt`) — the same `STENCIL_LLM_*` keys as every other surface.
Only `model` is overridable per call; the provider and its endpoint are operator
configuration, so a caller cannot redirect the configured key to a host of its choosing.
Getting a provider running: [root README → AI assistant](../README.md#ai-assistant--setting-up-a-model).

| Variable | Default | Meaning |
|---|---|---|
| `STENCIL_LLM_PROVIDER` | `ollama` | `ollama` \| `openai-compat` (LM Studio, llama.cpp, vLLM…) \| `stencil-server` |
| `STENCIL_LLM_BASE_URL` | `http://localhost:11434` (ollama) / `http://localhost:1234/v1` (openai-compat) | provider endpoint |
| `STENCIL_LLM_MODEL` | empty | model name (empty = server default) |
| `STENCIL_LLM_API_KEY` | empty | `openai-compat` bearer key (LM Studio needs none) |
| `STENCIL_LLM_SERVER_URL` | — | `stencil-server` only: a Stencil collaboration server proxying Anthropic |
| `STENCIL_LLM_SERVER_TOKEN` | empty | bearer token for that server |

> **Plain-http only, and credentials stay on the box.** The transport is a small hand-rolled
> HTTP/1.1 client — `https://` endpoints are rejected, and `Authorization` / `x-api-key`
> headers are sent only to loopback, so a key can never leave in cleartext. Use a local
> provider, a local TLS proxy, or a stencil-server on loopback; the collaboration server
> holds the Anthropic key and does the TLS hop.

## Usage

The server speaks MCP over **stdio**; register it with a client rather than running it by
hand:

```bash
# Claude Code:
claude mcp add stencil -- /path/to/stencil-mcp
# …or with an explicit CLI path:
STENCIL_CLI=/path/to/stencil claude mcp add stencil -- /path/to/stencil-mcp
```

### Tools

<!-- generated from toolDescriptions.json — rewrite with `MCP_UPDATE_PROSE=1 cargo test` -->
| Tool | Purpose | Key parameters |
|---|---|---|
| `stencil_edit` | Run the full pipeline, write a file, deliver to surface(s), optionally fetch/publish on a collaboration server | `input` \| `blank`, `crop`, `rotate`, `layout`, `filter`, `frame`, `output`, `overwrite`, `surface`, `server`, `remote_update`, `remote`, `remote_name` |
| `stencil_probe` | Read an image's pixel size | `input` |
| `source_site` | Scrape a web page and download its matching media into a **directory** | `source_site`, `output`, `count`, `group`, `filter`, `format`, `min_width`/`max_width`/`min_height`/`max_height` |
| `stencil_prompt` | Ask a configured LLM to plan edits from natural language and run them (see [LLM assistant](#llm-assistant-stencil_prompt)) | `prompt`, `input`, `output_dir`, `model` override |
<!-- /generated -->

`stencil_edit` maps directly onto the CLI (`source → crop → rotate → filter → layout →
encode`):

- **`input`** — a local path or `http(s)://` URL to an image or video. Mutually exclusive
  with **`blank`** (`{ page?, width?, height?, color? }`; `page` is an ISO format name,
  mutually exclusive with explicit `width`/`height`; omit all for A4 @ 96dpi).
- **`crop`** — a spec string (`"x1=10% x2=90% y1=10% y2=90%"`) or an object of edges
  (`{ x1, x2, y1, y2 }`). Each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a
  bare pixel delta; a leading `-` measures from the far edge. `album: true` derives a missing
  axis from the page proportion.
- **`rotate`** — quarter-turns clockwise (`1` = 90°, `-1` = −90°, `2` = 180°).
- **`layout`** — a path/URL string or an inline layout object (same schema the browser
  exports); coordinates are image pixels.
- **`filter`** — `bw`, `sepia`, `invert`, `contour`, or a CSS color / `#hex` (duotone
  tint); overrides a filter baked into the layout.
- **`output`** — the result path (extension auto-filled from the input format). `overwrite`
  defaults to `false`.
- **`frame`** — video frame index (0-based); needs `ffmpeg` on `PATH`.
- **`surface`** — override the configured delivery for this call. The result includes a
  per-surface `deliveries` array (each with `ok`, `detail`, and any `url`).
- **`server`** / **`remote_update`** / **`remote`** / **`remote_name`** — see
  [Collaboration server](#collaboration-server).

#### Scraping a page (`source_site`)

`source_site` fetches a web page, extracts the media it references (the CLI parses the
HTML), filters that set, and downloads the matches into a directory. Its `output` is a
**directory** (created if missing; default `.`), and it writes files locally only.

| Parameter | Effect |
|---|---|
| `source_site` (URL) | The page to scrape (`http(s)://`). Required. |
| `output` (dir) | Destination directory for the downloads. Default `.`. |
| `filter` | Category tokens, `\|`-separated: `img`, `video`, `background`, `poster`. Default all. |
| `format` | Format tokens, `\|`-separated normalized extensions (`png\|jpg\|webp\|mp4`…). Default all. |
| `min_width` / `max_width` / `min_height` / `max_height` | Inclusive px bounds, measured from image bytes; video/unmeasurable items always pass. |
| `count` | Items per page — omit for the CLI's default of **5**; `0` = **all** matches. |
| `group` | 0-based page index over the filtered list; needs `count`. |

Returns `{ dir, host, files: [{ path, width, height }] }` (`width`/`height` are `null` for
video and unmeasured items) plus a `wrote …` / `scraped N file(s) …` text summary.

```jsonc
{ "name": "source_site", "arguments": {
    "source_site": "https://example.com/gallery", "output": "shots",
    "filter": "img", "format": "png|jpg", "min_width": 400, "count": 10 } }
```

#### LLM assistant (`stencil_prompt`)

`stencil_prompt` implements the shared [LLM contract](../contracts/llm/llm-contract.md): it
sends the `prompt` (and, for vision, a local `input` image ≤ 8 MiB) to the configured
provider, strictly validates the returned op-plan, and executes it through the same CLI
pipeline as `stencil_edit`. The plan's base actions are written to `{output_dir}/result.png`
and each variant to `{output_dir}/{sanitized-label}.png`; a `save` op writes
`{output_dir}/{name}.stencil`. A plan with no actions is a chat-only answer — text back,
nothing written.

A turn is one model round, with one exception: a plan whose actions only load a picture the
model has not seen (`blank`/`frame` with no layout drawn) is applied and the prompt is re-sent
**once** with the rendered image attached, so "create a blank page and draw something on it"
completes in one call. The tool is single-turn (no history between calls), carries exactly
one image (an `image` op with index 1 restarts from it; higher indexes are skipped with a
note), does not downscale attachments, and collapses a plan into the CLI's fixed pipeline
order — plans needing two crops on one image, a `formula`, a standalone `page`, or several
frames are rejected with an explanation.

```jsonc
// "rotate photo.jpg right and give me a sepia and a b&w variant" → out/result.png,
// out/sepia.png, out/b-w.png
{ "name": "stencil_prompt", "arguments": {
    "prompt": "rotate it right and give me a sepia and a b&w variant",
    "input": "photo.jpg", "output_dir": "out" } }
```

#### Collaboration server

`stencil_edit` drives the CLI's server client, so the same tool both fetches a server project
to edit and publishes results. The result is always saved locally too.

| Parameter | Effect |
|---|---|
| `server` (URL) + `input` (name) | Connect to the server and treat `input` as a **project name**: fetch that project's image and edit it. Incompatible with `blank`. |
| `remote_update` (bool) | With `server`, write the edited result **back into** the fetched project. |
| `remote` (URL) | Publish the result as a **new** project on this server — from a local/web `input`, a `blank`, or a `server`-fetched project. |
| `remote_name` (string) | Name for the `remote` project (default: the input image's base name). |

`server` and `remote` can point at **different** servers, so one call can fetch a project
from server A and publish it to server B. The result's `server` array reports each delivery
(`{ action: "updated", id, width, height }` or `{ action: "created", name, id }`). The CLI's
interactive multi-server console (`/connect`, `/sync`, live update notices) is a stdin REPL
and out of scope for the MCP.

Inline `layout` object (a polyline through `points`; close + fill a shape by repeating the
first point and setting a non-`transparent` `fillColor`):

```jsonc
{
  "imageWidth": 800, "imageHeight": 600, "filter": "none",
  "lines": [
    {
      "points": [ { "x": 50, "y": 50 }, { "x": 750, "y": 50 }, { "x": 400, "y": 550 } ],
      "color": "#ff0000", "thickness": 3, "pointSize": 0,
      "style": "solid", "locked": false, "fillColor": "transparent"
    }
  ]
}
```

### Example prompts

Once registered, ask your MCP client in plain language — it picks the tool and fills the
arguments. The configured `surface` default decides where each result is delivered (add
"open it in the browser/desktop" to override per request):

- "Crop **photo.jpg** to its center 80%, rotate it a quarter-turn right, and save it as
  **out.png**."
- "Make **scan.png** black and white and save as **scan-bw.png**."
- "Create a blank red 800×600 canvas, draw a blue rectangle around the middle, tone it
  sepia, and save as **card.png**."
- "Grab frame 24 of **clip.mp4** as **frame.png** and open it in the browser editor."
- "What are the pixel dimensions of **photo.jpg**?"
- "On the server **http://host:8090**, open the project **Floor plan**, tone it sepia, and
  save the result back to that project."
- "Fetch **Plans** from **http://a:8090**, crop it to the top half, and publish the result as
  a new project on **http://b:8090**."

### Example tool calls

```jsonc
// center-crop to 80% and rotate a quarter-turn clockwise
{ "name": "stencil_edit", "arguments": {
    "input": "photo.jpg", "crop": "x1=10% x2=90% y1=10% y2=90%", "rotate": 1, "output": "out.png" } }

// blank red 800x600, draw a box, tone it sepia
{ "name": "stencil_edit", "arguments": {
    "blank": { "width": 800, "height": 600, "color": "red" }, "filter": "sepia",
    "layout": { "lines": [ { "points": [ {"x":100,"y":100}, {"x":700,"y":100},
      {"x":700,"y":500}, {"x":100,"y":500}, {"x":100,"y":100} ], "color": "#00f" } ] },
    "output": "card.png" } }

// blank B5 page (ISO format name instead of pixel dims)
{ "name": "stencil_edit", "arguments": { "blank": { "page": "B5" }, "output": "page.png" } }

// grab the 24th frame of a video
{ "name": "stencil_edit", "arguments": { "input": "clip.mp4", "frame": 24, "output": "frame.png" } }

// crop, then deliver to the CLI file AND open it in the browser editor
{ "name": "stencil_edit", "arguments": {
    "input": "photo.jpg", "crop": "x1=10% x2=90% y1=10% y2=90%",
    "surface": ["cli", "browser"], "output": "out.png" } }

// read an image's dimensions
{ "name": "stencil_probe", "arguments": { "input": "photo.jpg" } }

// fetch the server project "Floor plan", tone it sepia, write the result back to it
{ "name": "stencil_edit", "arguments": {
    "server": "http://host:8090", "input": "Floor plan", "filter": "sepia",
    "remote_update": true, "output": "floor.png" } }

// fetch from one server, publish the result to another (two connections, one call)
{ "name": "stencil_edit", "arguments": {
    "server": "http://a:8090", "input": "Plans", "crop": "y2=50%",
    "remote": "http://b:8090", "remote_name": "Plans (top)", "output": "plans.png" } }
```
