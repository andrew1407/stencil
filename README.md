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

A companion **Chrome extension** ([`browser-extension/`](browser-extension/)) feeds the browser editor: it
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

| Subproject | Guide | Covers |
|---|---|---|
| Core | [core/README.md](core/README.md) | build & test the shared library |
| Browser | [browser/README.md](browser/README.md) | serve & test the app, the optional single-file build |
| Desktop | [desktop/README.md](desktop/README.md) | build, test & run the Qt app |
| CLI | [cli/README.md](cli/README.md) | build, test & run the Zig tool |
| Python | [pystencil/README.md](pystencil/README.md) | build & test the package |
| MCP server | [mcp/README.md](mcp/README.md) | build, test & register the server |
| Collaboration server | [server/README.md](server/README.md) | build, test & run the Go server |
| Telegram bot | [bot/README.md](bot/README.md) | build, test & run the bot |
| Chrome extension | [browser-extension/README.md](browser-extension/README.md) | load unpacked & test |
| E2E harness | [e2e/README.md](e2e/README.md) | run the cross-surface smoke suite |

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
crop 10% off each side", "give me 3 variants: rotated, tinted, contoured") and the model
answers with a validated *op-plan* that runs through the very same operations the toolbar and
CLI flags use. Stencil ships **no model and no API key** — you point it at one. Three provider
choices, identical everywhere:

| Provider | What it is | Default endpoint |
|---|---|---|
| `ollama` | a local Ollama daemon (native `/api/chat`) | `http://localhost:11434` |
| `openai-compat` | any OpenAI-style server — LM Studio, `llama.cpp`, vLLM | `http://localhost:1234/v1` |
| `stencil-server` | a [collaboration server](server/) proxying its own upstream key, which never leaves it | your configured server URL |

The GUI surfaces (browser, desktop, extension) additionally offer **`none` — assistant off**:
nothing is probed or sent and the assistant UI is hidden.

**Pick a vision-capable model.** Every turn ships the image you are working on, so a text-only
model still handles "crop 10% off each side" but cannot "outline the rabbit's head".

Where you set it, per surface:

| Surface | Where you set it | Stored as |
|---|---|---|
| [browser](browser/README.md) | the chat's **…** menu ▸ **Settings** (or `Alt+Shift+G`); scriptable as `stencil.llm` | `localStorage` key `drawingApp_llmSettings` |
| [desktop](desktop/README.md) | the chat dock's **…** menu ▸ **Settings** (or `Alt+Shift+G`, **View ▸ AI Assistant Settings…**), or **Settings ▸ AI assistant** | settings JSON: `llmProvider`, `llmBaseUrl`, `llmModel`, `llmApiKey`, `llmServerUrl` |
| [browser-extension](browser-extension/README.md) | **Options → AI assistant** | `chrome.storage.local` key `llmSettings` (+ `serverToken`) |
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

Conversations are session-only unless you opt in to **chat persistence** (off by default on
every surface: a "Save chats with projects" toggle in the browser and desktop, `/chat on|off`
in the cli/pystencil consoles, `/chat save on|off` in the bot).

Each surface's own README carries its settings UI and its endpoint keys; the collaboration
server's proxy keys are in [server/README.md](server/README.md#llm-proxy). The normative spec
is [`llm-contract.md`](contracts/llm/llm-contract.md), with provider wire mappings in
[`llm-providers.md`](contracts/llm/llm-providers.md), per-surface op profiles in
[`llm-profiles.md`](contracts/llm/llm-profiles.md) and chat persistence in
[`llm-chat.md`](contracts/llm/llm-chat.md).

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
