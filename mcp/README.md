# Stencil — MCP server (Rust)

A Model Context Protocol (MCP) server that exposes Stencil's image/video editing pipeline as
tools any MCP client (Claude Code, Claude Desktop, or your own agent) can call: load an
image, video frame, or blank page, crop / rotate it, draw a layout, apply a filter, and write
the result — read an image's size and format, scrape a web page's media, check, preview, run
or convert `.stc` scripts, or list, fetch, edit, update and publish projects on a Stencil
[collaboration server](../server/README.md). It shells out to
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
the resolved default. Copy [`.env.example`](.env.example) to `.env` (gitignored) — beside the
`stencil-mcp` binary, or in this `mcp/` directory of the checkout it was built from — to set
local values. The server never reads a `.env` (or finds a CLI) in the directory it is started
from, since that can be an untrusted workspace:

| Variable | Default | Meaning |
|---|---|---|
| `STENCIL_SURFACES` | `cli` | default surfaces as a comma list, e.g. `cli,desktop,browser` (`cli` is always included) |
| `STENCIL_CLI` | auto-discovered | path to the `stencil` CLI binary (`STENCIL_CLI` → the `cli/zig-out/bin/stencil` of the checkout this server was built in → `PATH`) |
| `STENCIL_CLI_TIMEOUT_SECONDS` | `120` | per-invocation deadline; a CLI run past it is killed and reported as an `error:` |
| `STENCIL_MCP_MAX_CONCURRENT_CLI` | `2` | how many CLI runs may go at once across all calls; the rest wait their turn |
| `STENCIL_MCP_ROOTS` | the working directory | directories every write must land in, as a path list (`:`-separated; `;` on Windows) — used when the client offers no roots of its own |
| `STENCIL_MCP_SERVERS` | none | the collaboration servers a call may reach, as a comma list of origins (`http://localhost:8090,https://team.example`); without it `server`, `remote` and the project tools (`stencil_projects`, `stencil_project_update`, `stencil_project_file`) are refused |
| `STENCIL_MCP_SERVER_TOKENS` | none | the token for each allowed server, as `origin=token` pairs, comma-separated; never a tool parameter, and handed to the CLI in its environment, never on its command line |
| `STENCIL_DESKTOP` | `<checkout>/desktop/build/stencil` | path to the Qt desktop binary |
| `STENCIL_BROWSER_URL` | `http://localhost:8080` | base URL of the served editor — only for the browser surfaces, only if not on the default |
| `STENCIL_AUTO_OPEN` | `false` | open the browser URL with the OS opener (`open`/`xdg-open`) |

The per-call `surface` parameter accepts a JSON array (`["cli","browser"]`) or a single
string (`"browser"`). The `browser` surface builds a launch URL that carries the image itself,
so it only works for small results: up to about 32 KiB of URL returned in the result, or 96
KiB when `STENCIL_AUTO_OPEN` opens it directly. A larger result is reported with the path to
open instead.

**Roots.** Every write — an `output`, an `output_dir`, a script's `@save` — must land inside
one of the server's roots: the workspace roots the MCP client offers, if it offers any, else
`STENCIL_MCP_ROOTS`, else the directory the server was started in. A
relative path resolves against the first root; anything outside every root is refused before
the CLI runs, and the CLI's own `--confine-output` refuses it again.

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
| `stencil_edit` | Run the full pipeline, write a file, deliver to surface(s), optionally fetch/publish on a collaboration server | `input` \| `blank`, `crop`, `rotate`, `layout`, `layout_frame`, `filter`, `frame`, `output`, `overwrite`, `preview`, `surface`, `server`, `remote_update`, `remote`, `remote_name` |
| `stencil_probe` | Read an image's pixel size, format, alpha and byte size, or a video's length | `input` |
| `source_site` | Scrape a web page and download its matching media into a **directory** | `source_site`, `output`, `count`, `group`, `filter`, `format`, `min_width`/`max_width`/`min_height`/`max_height` |
| `stencil_prompt` | Ask a configured LLM to plan edits from natural language and run them (see [LLM assistant](#llm-assistant-stencil_prompt)) | `prompt`, `input`, `output_dir`, `model` override |
| `stencil_script` | Run a `.stc` script — batch edits over files, directories or globs, confined to one output directory | `script_text` \| `script_path`, `input`, `output_dir`, `preview` |
| `stencil_script_check` | Validate a `.stc` script without running it; structured diagnostics | `script_text` \| `script_path` |
| `stencil_script_plan` | Dry-run a `.stc` script: its inputs, op-plans and save paths | `script_text` \| `script_path`, `input` |
| `stencil_script_emit` | Rewrite a `.stc` script as JavaScript for the browser or Python for pystencil | `script_text` \| `script_path`, `output`, `overwrite` |
| `stencil_projects` | List a collaboration server's projects, or read one's metadata and stored files | `server`, `id`, `limit`, `after`, `files` |
| `stencil_project_update` | Change a server project's name, description, keywords, colours or expiry | `server`, `id`, `name`, `description`, `keywords`, `color`, `blank_color`, `expires_at`, `if_version` |
| `stencil_project_file` | Download a file a server project stores: its original, result, video, chat or a variant | `server`, `id`, `kind`, `output`, `overwrite` |
<!-- /generated -->

Every successful result carries a text summary, the same payload as a JSON text block, and
that payload as structured content matching the tool's published output schema. Tools are
annotated read-only or destructive, and open-world where they reach the network. A client
that sends a progress token receives a progress notification per written file, scrape step
and LLM round; a cancelled call stops at once and takes its CLI run with it.

`stencil_edit` maps directly onto the CLI (`source → crop → rotate → filter → layout →
encode`):

- **`input`** — a local path or `http(s)://` URL to an image or video. Mutually exclusive
  with **`blank`** (`{ page?, width?, height?, color? }`; `page` is an ISO format name,
  mutually exclusive with explicit `width`/`height`; omit all for A4 @ 96dpi).
- **`crop`** — a spec string (`"x1=10% x2=90% y1=10% y2=90%"`) or an object of edges
  (`{ x1, x2, y1, y2 }`). Each edge is a length token: `px`, `cm`, `mm`, `in`, `%`, or a
  bare pixel number (`10` or `"10"`); a leading `-` measures from the far edge. `album: true`
  derives a missing axis from the page proportion.
- **`rotate`** — quarter-turns clockwise, `-3`…`3` (`1` = 90°, `-1` = −90°, `2` = 180°).
  A value in degrees is refused with the quarter-turn count it meant.
- **`layout`** — a path/URL string, an inline layout object (same schema the browser
  exports), or that object as a JSON string; coordinates are image pixels.
- **`layout_frame`** — `"current"` (default: the image after crop and rotate) or `"source"`
  (the input as loaded; the CLI re-maps the lines through the crop and rotation).
- **`filter`** — `bw`, `sepia`, `invert`, `contour`, or a CSS color / `#hex` (duotone
  tint); overrides a filter baked into the layout.
- **`output`** — the result path, inside the roots (extension auto-filled from the input
  format; a name ending in `.stencil` bundles a project instead of an image). `overwrite`
  defaults to `false`: the CLI then refuses an existing file under the name it would actually
  write, extension filled in.
- **`frame`** — video frame index (0-based); needs `ffmpeg` on `PATH`.
- **`surface`** — override the configured delivery for this call. The result includes a
  per-surface `deliveries` array (each with `ok`, `detail`, and any `url`).
- **`preview`** — `true` adds the written image as a PNG thumbnail (longer side at most
  512 px, never enlarged) in an image content block after the JSON one, and its size as
  `preview` in the payload, so a vision-capable client can check the edit. Off by default;
  `stencil_script` takes it too and previews the first four images a run wrote.
- **`server`** / **`remote_update`** / **`remote`** / **`remote_name`** — see
  [Collaboration server](#collaboration-server).

#### Resources and prompts

The server also offers read-only **resources**, each a canonical Stencil file:
`stencil://config/opRegistry.json` (the op-plan registry), `stencil://contracts/stc-contract.md`
(the `.stc` language), `stencil://config/page-formats.json` (ISO page sizes in cm),
`stencil://config/colors.json` (colour names) and `stencil://config/filters.json` (filter
modes). Two **prompts** start common work: `write_stc_script` (draft, check, preview and run a
script for a `task`) and `print_page_layout` (lay `content` out on an ISO `page`).

#### Scraping a page (`source_site`)

`source_site` fetches a web page, extracts the media it references (the CLI parses the
HTML), filters that set, and downloads the matches into a directory. Its `output` is a
**directory** (created if missing; default `.`), and it writes files locally only.

| Parameter | Effect |
|---|---|
| `source_site` (URL) | The page to scrape (`http(s)://`). Required. |
| `output` (dir) | Destination directory for the downloads, inside the roots. Default: the first root. |
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
provider, has the CLI strictly validate the returned op-plan (`stencil --plan-check`), and
executes it through the same CLI pipeline as `stencil_edit`. Build the CLI from the same
checkout as the server: one built from a different op registry is reported at startup, and
its plans are refused. The plan's base actions are written to `{output_dir}/result.png`
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

#### Scripts (`stencil_script`)

`stencil_script` runs a Stencil script — a `.stc` file in the batch language documented in
[`contracts/stc/stc-contract.md`](../contracts/stc/stc-contract.md). The script names its own
inputs (`@source <file|url|dir/|glob>:` blocks) and its own outputs (`@save`), so one call can
edit a whole directory.

| Parameter | Effect |
|---|---|
| `script_text` | The `.stc` source inline (up to 256 KiB). Written to a temp file for the run. Mutually exclusive with `script_path`. |
| `script_path` | An existing `.stc` file to run instead. |
| `input` | Working image for a script with **no** `@source` block (a path or `http(s)://` URL). |
| `output_dir` | Directory every `@save` must write inside, itself inside the roots (created if missing; default: the first root). |

The run is confined to `output_dir`, so write **relative** `@save` targets — an absolute path
(and so a bare `@save` beside an absolute source) is refused by the sandbox, and a `@save` into
a subdirectory needs that directory to already exist. A script with any diagnostic error runs
nothing; the call fails with the CLI's `file:line:col: error: … [CODE]` lines. Success returns
every image written with its pixel dimensions, every `.stencil` project, and the run's
notes.

```jsonc
// tone every PNG in a folder, results named <base>-stencil.png in the run directory
{ "name": "stencil_script", "arguments": {
    "script_text": "@source /work/shots/*.png:\n  @filter sepia\n  @save ./\n",
    "output_dir": "run" } }

// a script with no @source block edits the image the call names
{ "name": "stencil_script", "arguments": {
    "script_path": "recipes/thumb.stc", "input": "photo.jpg", "output_dir": "run" } }
```

Three tools read a script without running it. **`stencil_script_check`** returns every
diagnostic — line, column, severity, the stable code from the contract's catalogue, message —
and `valid`. **`stencil_script_plan`** returns what a run would do: per `@source` block, the
expanded inputs, the op-plan actions and the concrete path of every `@save` (pass `input` for a
script with no `@source` block). **`stencil_script_emit`** writes the script out for another
surface, the `output` extension picking the target: `.js`/`.stcjs` for the browser editor's
console, `.py`/`.pystc` for pystencil.

```jsonc
{ "name": "stencil_script_check", "arguments": { "script_text": "@crop 10%\n@save out\n" } }
{ "name": "stencil_script_emit", "arguments": { "script_path": "tour.stc", "output": "tour.pystc" } }
```

#### Collaboration server

`stencil_edit` drives the CLI's server client, so the same tool both fetches a server project
to edit and publishes results. The result is always saved locally too. Only the servers in the
operator's `STENCIL_MCP_SERVERS` are reachable, each with its token from
`STENCIL_MCP_SERVER_TOKENS`; a caller names a server but never a token. `stencil_projects`
lists an allowed server's projects — the names `server` + `input` fetch — or reads one
project's metadata by `id`, paged with `limit` and `after`; with `files` it also lists the
files the project stores. `stencil_project_update` changes a project's name, description,
keywords, colours or expiry, guarded by its version so a peer's newer save is never
overwritten unseen, and `stencil_project_file` downloads one stored file — the original, the
result, a video, the saved chat or a variant — into your roots. Every read and edit goes
through the CLI, so `http://` and `https://` servers both work.

| Parameter | Effect |
|---|---|
| `server` (URL) + `input` (name) | Connect to the server and treat `input` as a **project name**: fetch that project's image and edit it. Incompatible with `blank`. |
| `remote_update` (bool) | With `server`, write the edited result **back into** the fetched project. |
| `remote` (URL) | Publish the result as a **new** project on this server — from a local/web `input`, a `blank`, or a `server`-fetched project. |
| `remote_name` (string) | Name for the `remote` project (default: the input image's base name). |

`server` and `remote` can point at **different** allowed servers, so one call can fetch a
project from server A and publish it to server B, each with its own operator token. The
result's `server` array reports each delivery
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
- "Check **tour.stc** and tell me what it would write before running it."
- "Which projects are on **http://localhost:8090**?"
- "Rename the project **p_1_a** on **http://localhost:8090** to **Plans v2** and tag it
  **floor**, then download its result as **plans.png**."
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

// sepia, and get a 512 px thumbnail of the result back to look at
{ "name": "stencil_edit", "arguments": {
    "input": "photo.jpg", "filter": "sepia", "output": "out.png", "preview": true } }

// crop, then deliver to the CLI file AND open it in the browser editor
{ "name": "stencil_edit", "arguments": {
    "input": "photo.jpg", "crop": "x1=10% x2=90% y1=10% y2=90%",
    "surface": ["cli", "browser"], "output": "out.png" } }

// read an image's dimensions, format and alpha, or a video's length
{ "name": "stencil_probe", "arguments": { "input": "photo.jpg" } }

// list the first page of an allowed server's projects
{ "name": "stencil_projects", "arguments": { "server": "http://localhost:8090", "limit": 20 } }

// read one project and the files it stores, rename it, and download its result
{ "name": "stencil_projects", "arguments": { "id": "p_1_a", "files": true } }
{ "name": "stencil_project_update", "arguments": { "id": "p_1_a", "name": "Plans v2", "keywords": ["floor"] } }
{ "name": "stencil_project_file", "arguments": { "id": "p_1_a", "kind": "result", "output": "plans.png" } }

// fetch the server project "Floor plan", tone it sepia, write the result back to it
{ "name": "stencil_edit", "arguments": {
    "server": "http://host:8090", "input": "Floor plan", "filter": "sepia",
    "remote_update": true, "output": "floor.png" } }

// fetch from one server, publish the result to another (two connections, one call)
{ "name": "stencil_edit", "arguments": {
    "server": "http://a:8090", "input": "Plans", "crop": "y2=50%",
    "remote": "http://b:8090", "remote_name": "Plans (top)", "output": "plans.png" } }
```
