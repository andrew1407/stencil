# Stencil

<p align="center">
  <a href="https://andrew1407.github.io/stencil/"><img src="browser/favicon.svg" alt="Stencil logo" width="120" height="120"></a>
</p>

[![CI](https://github.com/andrew1407/stencil/actions/workflows/ci.yml/badge.svg)](https://github.com/andrew1407/stencil/actions/workflows/ci.yml)
[![Desktop packages](https://github.com/andrew1407/stencil/actions/workflows/desktop-packages.yml/badge.svg)](https://github.com/andrew1407/stencil/actions/workflows/desktop-packages.yml)
[![Deploy to GitHub Pages](https://github.com/andrew1407/stencil/actions/workflows/pages.yml/badge.svg)](https://github.com/andrew1407/stencil/actions/workflows/pages.yml)

An image annotation / drawing tool: load an image, draw polylines and rectangles over
it, edit points numerically, convert pixel coordinates to page (cm) coordinates with
optional `f(x,y)` formula transforms, and save your work.

Stencil ships as **one shared logic core with a family of front-ends and services**:

| App | Path | Stack | Docs |
|---|---|---|---|
| **Core** | [`core/`](core/) | C++17, STL-only, GUI-free shared library | [core/README.md](core/README.md) |
| **Browser** | [`browser/`](browser/) | Vanilla ES-module JS, no build step (optional single-file bundle) | [browser/README.md](browser/README.md) |
| **Desktop** | [`desktop/`](desktop/) | C++17 + Qt 6, CMake build | [desktop/README.md](desktop/README.md) |
| **CLI** | [`cli/`](cli/) | Zig, wraps the C++ core | [cli/README.md](cli/README.md) |
| **Python** | [`pystencil/`](pystencil/) | Stdlib-only Python, drives the C++ core via ctypes | [pystencil/README.md](pystencil/README.md) |
| **MCP server** | [`mcp/`](mcp/) | Rust, exposes the CLI's pipeline as MCP tools | [mcp/README.md](mcp/README.md) |
| **Collaboration server** | [`server/`](server/) | Go, stores/shares projects + live multi-client edit sessions | [server/README.md](server/README.md) |
| **Telegram bot** | [`bot/`](bot/) | .NET (C#), chat-driven editing over the CLI + server REST | [bot/README.md](bot/README.md) |

A companion **Chrome extension** ([`extension/`](extension/)) feeds the browser editor: it
lists, searches and filters every image on any web page and opens a chosen image in the
Stencil editor, with a quick in-page crop. The cross-surface smoke harness lives in
[`e2e/`](e2e/).

Every surface also carries an **AI assistant** — describe an edit in words and a model you
point Stencil at plans it. Setup guide: [AI assistant — setting up a model](#ai-assistant--setting-up-a-model).

How the pieces fit together — the shared core, the parity contract, the layer model per
app — is in [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Development

No dependencies to install anywhere: each subproject runs on its platform's built-in
tooling (the desktop needs Qt 6; the C++ tests use a single Doctest header fetched at
configure time; the CLI shells out to a system `ffmpeg` for video input only).

- Build & test the shared core → [core/README.md](core/README.md)
- Build & run the browser app → [browser/README.md](browser/README.md)
- Build, test & run the desktop app → [desktop/README.md](desktop/README.md)
- Build, test & run the CLI → [cli/README.md](cli/README.md)
- Build & test the Python package → [pystencil/README.md](pystencil/README.md)
- Build, test & run the MCP server → [mcp/README.md](mcp/README.md)
- Build, test & run the Telegram bot → [bot/README.md](bot/README.md)
- Load & test the Chrome extension → [extension/README.md](extension/README.md)
- Run the cross-surface e2e smoke harness → [e2e/README.md](e2e/README.md)

**Docker.** Five subprojects ship a multi-stage `Dockerfile`. The first four compile
`core/`, so **build them from the repo root** with `-f`; the server image builds from its
own `./server` context:

```bash
docker build -f browser/Dockerfile -t stencil-browser . && docker run --rm -p 8080:80 stencil-browser
docker build -f cli/Dockerfile -t stencil-cli . && docker run --rm -v "$PWD:/work" -w /work stencil-cli --help
docker build -f mcp/Dockerfile -t stencil-mcp . && docker run --rm -i -v "$PWD:/work" -w /work stencil-mcp
docker build -f bot/Dockerfile -t stencil-bot . && docker run --rm -e TELEGRAM_BOT_TOKEN=123:abc stencil-bot
docker build -t stencil-server ./server
```

For the full local stack — Postgres + Redis + the collaboration server (plus the browser
app and an on-demand `mcp` profile) — the repo-root [`docker-compose.yml`](docker-compose.yml)
wires it together; it also backs the full-stack [`e2e/`](e2e/) suite:

```bash
docker compose up --build server   # collab server on :8090 (REST/WS) + :8091 (TCP), with its db/redis
```

## AI assistant — setting up a model

Every surface ships an **LLM assistant**: you describe an edit in words ("make it sepia and
crop 10% off each side", "give me 3 variants: rotated, tinted, contoured", "extract the lines
from this image") and the model answers with a validated *op-plan* that runs through the very
same operations the toolbar / CLI flags use. Stencil ships **no model and no API key** — you
point it at one. Three provider choices, identical everywhere:

| Provider | What it is | Default endpoint |
|---|---|---|
| `ollama` | a local [Ollama](https://ollama.com) daemon (native `/api/chat`) | `http://localhost:11434` |
| `openai-compat` | any OpenAI-style local server — LM Studio, `llama.cpp`, vLLM | `http://localhost:1234/v1` |
| `stencil-server` | Anthropic Claude proxied by a [collaboration server](server/) — the key stays server-side | your configured server URL |

The GUI surfaces (browser, desktop, extension) additionally offer **`none` — assistant off**:
nothing is probed or sent and the assistant UI is hidden.

Conversations are session-only unless you opt in to **chat persistence** (off by default on
every surface: a "Save chats with projects" toggle in the browser and desktop, `/chat on|off`
in the cli/pystencil consoles, `/chat save on|off` in the bot).

The full spec is [`llm-contract.md`](llm-contract/llm-contract.md), with providers/wire
mappings in [`llm-providers.md`](llm-contract/llm-providers.md), per-surface op profiles in
[`llm-profiles.md`](llm-contract/llm-profiles.md) and chat persistence in
[`llm-chat.md`](llm-contract/llm-chat.md). This section is the setup guide.

### 1. Ollama (local, the default)

Install it from [ollama.com/download](https://ollama.com/download), then:

```bash
ollama serve                      # daemon on http://localhost:11434
ollama pull llama3.2-vision       # or: qwen2.5vl · llava — models that can SEE the image
ollama list                       # confirm what is installed
```

**Pick a vision-capable model.** Every turn ships the image you are working on; a text-only
model (`llama3.2`, `mistral`, …) cannot see it, so "crop 10% off each side" still works while
"outline the rabbit's head" needs a model that can see.

Then set **provider `ollama`**, base URL `http://localhost:11434`, model `llama3.2-vision`
(see [per-surface configuration](#4-per-surface-configuration)).

**Browser app only — CORS.** The browser calls Ollama from a page origin, so the daemon has to
allow it:

```bash
OLLAMA_ORIGINS='http://localhost:8080' ollama serve   # the origin `npm run serve` uses
```

No other surface needs this.

### 2. OpenAI-compatible (LM Studio, llama.cpp, vLLM)

Start whichever server you have, note its port, and use a base URL that **ends in `/v1`** —
the clients append `/chat/completions` to it:

```bash
# LM Studio: load a vision model → Developer tab → Start Server   →  http://localhost:1234/v1
llama-server -m ./model.gguf --host 127.0.0.1 --port 1234         #  http://localhost:1234/v1
vllm serve Qwen/Qwen2.5-VL-7B-Instruct --port 8000                #  http://localhost:8000/v1
```

Leave the model field empty to use whatever the server has loaded, or name it explicitly. The
optional **API key** (LM Studio and llama.cpp need none; a hosted endpoint does) goes in the
surface's API-key field / `STENCIL_LLM_API_KEY` — it is sent as `Authorization: Bearer …` on
this provider only.

**Browser app only:** flip LM Studio's **"enable CORS"** switch in its server settings.

### 3. Stencil collaboration server (Anthropic proxy)

Give the [Go server](server/) an Anthropic key and it proxies chat turns for every client
that connects to it — the key **never leaves the server**, and clients authenticate with the
bearer token they already have for it.

```bash
cd server
cp .env.example .env       # then set DATABASE_URL + the LLM keys below
go run ./cmd/stencil-server
```

Server-side env keys ([`server/.env.example`](server/.env.example), see
[server/README.md → LLM proxy](server/README.md#llm-proxy)):

| Key | Default | Meaning |
|---|---|---|
| `ANTHROPIC_API_KEY` | *(empty)* | **empty = proxy disabled**; the only place the key lives |
| `LLM_MODEL` | `claude-opus-5` | default model when a request names none |
| `LLM_BASE_URL` | `https://api.anthropic.com` | Anthropic API base |
| `LLM_MAX_TOKENS` | `8192` | cap; a request's `maxTokens` is clamped to it |
| `LLM_TIMEOUT_SECONDS` | `120` | outbound request timeout |

In each client, connect to the server as usual, then pick provider **`stencil-server`** and
point it at that server's URL. Auth reuses the stored connection's token; `mcp/` has no
connection store, so it takes an explicit `STENCIL_LLM_SERVER_TOKEN`; the CLI console and the
bot fall back to that key when no `/connect`-ed server matches.

### 4. Per-surface configuration

| Surface | Where you set it | Stored as |
|---|---|---|
| [browser](browser/README.md) | the chat's **…** menu ▸ **Settings** (or `Alt+Shift+G`); scriptable as `stencil.llm` | `localStorage` key `drawingApp_llmSettings` |
| [desktop](desktop/README.md) | the chat dock's **…** menu ▸ **Settings** (or `Alt+Shift+G`, **View ▸ AI Assistant Settings…**), or **Settings ▸ AI assistant** | settings JSON: `llmProvider`, `llmBaseUrl`, `llmModel`, `llmApiKey`, `llmServerUrl` |
| [extension](extension/README.md) | **Options → AI assistant** | `chrome.storage.local` key `llmSettings` (+ `serverToken`) |
| [cli](cli/README.md) | `STENCIL_LLM_*` env; `/llm provider\|url\|model\|key\|server <value>` in-session (bare `/llm` prints the config). Ask with `/prompt` | env + session state |
| [pystencil](pystencil/README.md) | same env and console commands; in code, `LlmConfig(provider=…, model=…)` | env / `LlmConfig` args |
| [bot](bot/README.md) | `STENCIL_LLM_*` (+ `STENCIL_LLM_SERVER_TOKEN`) in `bot/.env`. Ask with `/prompt` or `/chat` mode | env/`.env` |
| [mcp](mcp/README.md) | `STENCIL_LLM_*` + `STENCIL_LLM_SERVER_TOKEN` in `mcp/.env`; per-call `model` override on `stencil_prompt` | env/`.env` |

The env family is the same everywhere it applies: `STENCIL_LLM_PROVIDER`,
`STENCIL_LLM_BASE_URL`, `STENCIL_LLM_MODEL`, `STENCIL_LLM_API_KEY`, `STENCIL_LLM_SERVER_URL`
(plus `STENCIL_LLM_SERVER_TOKEN` for cli/mcp/bot). For example:

```bash
export STENCIL_LLM_PROVIDER=ollama
export STENCIL_LLM_BASE_URL=http://localhost:11434
export STENCIL_LLM_MODEL=llama3.2-vision
cd cli && ./zig-out/bin/stencil --console        # then: /prompt make it sepia
```

### 5. Verify it works

Probe the endpoint first — these are the same URLs the clients' status probes hit:

```bash
curl http://localhost:11434/api/tags            # ollama: the models you pulled
curl http://localhost:11434/api/version         # ollama: what the status dot checks
curl http://localhost:1234/v1/models            # openai-compat: the loaded model(s)
curl -H "Authorization: Bearer $TOKEN" http://localhost:8090/llm/info   # {"enabled":true,"model":"…"}
```

Then send a first prompt against a loaded image — `make it sepia` is the smallest end-to-end
check. A healthy setup shows a **green** status dot on the chat's settings gear (browser and
desktop; amber = still probing, red = unreachable), a bare `/llm` in the cli/pystencil
console prints the resolved provider, URL and model, and on every surface the reply text
appears **and** the image changes. A reply with no JSON object is just chat and edits nothing.

### 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Browser only: status red, DevTools shows a **CORS** error | The local provider isn't allowing the app's origin. Restart with `OLLAMA_ORIGINS='http://localhost:8080' ollama serve`, or enable LM Studio's CORS switch. |
| The model chats fine but ignores the picture | The model isn't **vision-capable** — `ollama pull llama3.2-vision` (or `qwen2.5vl` / `llava`), or load a vision model in LM Studio. |
| `openai-compat` returns **404** on every turn | The base URL is missing `/v1` — it must be `http://host:port/v1`. |
| `ollama` returns 404 | The opposite mistake: Ollama's base URL takes **no** `/v1`. |
| `503 llmDisabled` from a collaboration server | No `ANTHROPIC_API_KEY` in *that server's* environment. Check `GET /llm/info`; set the key and restart the server. |
| Turns hang or time out | First run on a local model can be slow (weights load on demand); for the server proxy raise `LLM_TIMEOUT_SECONDS`. |
| No assistant UI at all | The provider is **`none`**. Pick a real provider in the settings modal / Options. |
| mcp: "https rejected" | `mcp/`'s transport is **plain-http only** — use a local provider or an `http://` collaboration server. |

## Claude Code integration

This repo ships a Claude Code **skill** and **agent** for driving Stencil from the editor:

- **`/stencil` skill** ([`.claude/skills/stencil/SKILL.md`](.claude/skills/stencil/SKILL.md)) —
  headless image/video editing via the Zig CLI: crop, rotate, tint/filter, draw a layout,
  grab a video frame, or make a blank page — for local files or `http(s)` URLs.
- **`stencil-operator` agent** ([`.claude/agents/stencil-operator.md`](.claude/agents/stencil-operator.md)) —
  drives any Stencil front-end end-to-end (CLI, desktop, browser, extension), preferring the
  `window.stencil` scripting facade over clicking through the UI.

For a tool-protocol integration that works with **any** MCP client, the **MCP server**
([`mcp/`](mcp/)) exposes the same editing pipeline as `stencil_edit` / `stencil_probe` tools
over stdio — register it with `claude mcp add stencil -- /path/to/stencil-mcp`. See
[mcp/README.md](mcp/README.md).
