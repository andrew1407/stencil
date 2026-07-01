# Stencil — Telegram bot (.NET)

A Telegram bot that drives Stencil's image pipeline from a chat: upload a photo (or start a
blank canvas), crop / rotate / filter / draw a layout onto it, and download the result image
or its layout JSON — and connect to a Stencil [collaboration server](../server/README.md) to
list, fetch, create and save shared projects. Like `mcp/` and `pystencil/`, it is a **thin
adapter, not a core consumer**: it **shells out to the Zig CLI** (`cli/`) for every pixel
transform (so results match the browser, desktop, CLI and Python editors by construction) and
speaks the server's **REST** contract for projects. For the project overview see the
[repository README](../README.md).

Live instance: [@stencil_editor_bot](https://t.me/stencil_editor_bot).

```
You: (send a photo)
Bot: result.png — 1280x720    [🔄 Rotate] [B&W] [Sepia] [♻︎ Reset] [📄 JSON] …
You: /crop x1=10% x2=90% y1=10% y2=90%
You: /color #ff5623   /thickness 4
You: /draw rect 20%,20% 80%,80%   → annotates the image
You: /json            → downloads the layout JSON
You: /connect http://localhost:8090
You: /create Shared   → publishes the result as a new server project
```

## Dependencies

| Purpose | Tool | How it's provided |
|---|---|---|
| Build + language | **.NET 10** (C#) | the `dotnet` SDK |
| Telegram Bot API client | **Telegram.Bot** | the official library, a **project-local** NuGet `<PackageReference>` (see below) |
| Per-user session state (optional) | **StackExchange.Redis** | a project-local NuGet reference; only used when `REDIS_URL` is set |
| Hosting / DI / logging | **Microsoft.Extensions.*** | project-local NuGet references |
| The actual pixel/geometry work | **`../cli/`** (and through it, **`../core/`**) | invoked as a subprocess; **not** linked or recompiled here |

> The bot uses **only** the official `Telegram.Bot` library to talk to Telegram — no other
> third-party Bot API. Image work is the CLI's job; talking to the collaboration server is
> plain `System.Net.Http`. As with `mcp/`/`server/`, this subproject never links or
> recompiles `core/`, so the `core/` source-list parity rules don't apply to it — its only
> contracts are the CLI's documented flags and the server's REST routes.

### Packages are added locally, not globally

Every dependency is a per-project `<PackageReference>` in the relevant `.csproj` (restored
into the solution), **not** a global `dotnet tool`. That keeps the bot self-contained and
reproducible. If you ever need to re-add one, do it against the project, e.g.:

```bash
# from bot/ — adds a local <PackageReference> to that project's .csproj
dotnet add src/Stencil.TelegramBot.Bot package Telegram.Bot
```

Do **not** `dotnet tool install -g` anything for this bot.

## Architecture (clean architecture)

Five projects, dependencies pointing inward (`Domain` has no project references):

```
bot/
  Stencil.TelegramBot.slnx
  src/
    Stencil.TelegramBot.Domain/          entities, value objects, abstractions — the frozen contract
      Layout/        LayoutPoint · LayoutLine · StencilLayout  (the shared layout JSON schema)
      Editing/       EditState · EditRequest · BlankSpec · RenderResult · ImageSize
      Projects/      ProjectRecord · ProjectFull · Create/UpdateProjectRequest · FileWriteResult
      Sessions/      UserSession · ServerConnectionInfo
      Abstractions/  IStencilCli · IStencilServerClient(+Factory) · ISessionStore · IUserWorkspace
      Serialization/ StencilJson  (one camelCase JsonSerializerOptions shared everywhere)
      Exceptions/    StencilCliException · ServerException
    Stencil.TelegramBot.Application/      use cases over the Domain abstractions
      Editing/       IEditingService + EditingService   (one base image + a replayable EditState)
      Servers/       IServerService + ServerService      (connect/list/fetch/create/save)
    Stencil.TelegramBot.Infrastructure/   the adapters (depend only on Domain)
      Cli/           StencilCliLocator · CliArgvBuilder · CliOutcomeParser · ProcessStencilCli
      Server/        UrlNormalizer · HttpStencilServerClient · StencilServerClientFactory
      Sessions/      InMemorySessionStore · RedisSessionStore
      Workspace/     UserWorkspace        (per-user scratch dir for working images)
      Configuration/ BotOptions · DotEnv
    Stencil.TelegramBot.Bot/              the Telegram presentation + console host
      Program.cs · Telegram/{UpdateRouter, CommandParser, CommandHandlers, CallbackAction, Keyboards, Replies}
  tests/
    Stencil.TelegramBot.Tests/            xUnit — offline (no token, server, CLI or Redis)
```

- **`IStencilCli` → the Zig CLI.** `ProcessStencilCli` locates the binary (`STENCIL_CLI` →
  the repo's `cli/zig-out/bin/stencil` → `stencil` on `PATH`), runs it with `NO_COLOR=1`, and
  parses its `wrote {path} ({w}x{h})` / `error: …` stderr — a direct port of `mcp/src/{locate,
  args,outcome,pipeline}.rs`.
- **`IStencilServerClient` → the Go server's REST API.** `HttpStencilServerClient` is a port
  of `pystencil/pystencil/server.py` (`/auth/token`, `/projects[...]`, file upload/download,
  `{code,message}` → `ServerException`, last-writer-wins version guard).
- **One base image + a replayable edit state.** Each user has one original on disk plus an
  `EditState` (crop/rotate/filter/layout); every render replays it through the CLI, so the
  result is reproducible and the layout JSON is exportable. Mirrors the CLI console's single
  working image.
- **Sessions** live in Redis when `REDIS_URL` is set (the same store the Go server uses for
  fan-out), else in an in-memory map — so dev and tests need no external services.

## Configuration

Copy the template and set at least the token:

```bash
cp bot/.env.example bot/.env   # then paste your @BotFather token into TELEGRAM_BOT_TOKEN
```

Real environment variables always win over `.env`. The real `bot/.env` is gitignored.

| Variable | Default | Purpose |
|---|---|---|
| `TELEGRAM_BOT_TOKEN` | — (**required**) | Bot token from [@BotFather](https://t.me/BotFather) |
| `STENCIL_CLI` | auto-discovered | Path to the `stencil` CLI binary |
| `REDIS_URL` | — (in-memory) | Redis for per-user session state |
| `STENCIL_BOT_DATA_DIR` | `<temp>/stencil-bot` | Scratch dir for working images |
| `STENCIL_TLS_INSECURE` | `false` | Accept self-signed certs for `https` servers (dev) |

## Build · test · run

```bash
# from bot/
dotnet build Stencil.TelegramBot.slnx          # build all five projects
dotnet test  Stencil.TelegramBot.slnx          # 63 offline tests — no token/server/CLI/Redis needed
dotnet run --project src/Stencil.TelegramBot.Bot   # run the bot (needs TELEGRAM_BOT_TOKEN + the CLI)
```

Build the CLI first so the bot can shell out to it: `cd cli && zig build`.

The test suite is deliberately **offline**: argv building and stderr parsing (ports of the
MCP suites), URL normalisation (port of the pystencil suite), CLI locator, `.env` parsing,
layout/protocol JSON round-trips, the in-memory session store, the REST client against a
stub `HttpMessageHandler`, and the editing/server services against hand-written fakes. It
never reads `TELEGRAM_BOT_TOKEN`.

## Chat surface

Every slash command has a matching inline **button** (the buttons mirror the commands, like
the browser toolbar mirrors the console).

**Sources** — set the working image by sending a **photo** or an **image file** (compressed
or uncompressed both work), or by pasting an **image link** (a bare `http(s)` URL loads like
`/url`). Add a **caption command** to apply it immediately — e.g. a photo captioned
`/crop x1=10% x2=90% y1=10% y2=90%`, `/filter bw`, or `/draw rect 20%,20% 80%,80%`. Send a
**video** (or video file) to grab a frame — caption `/frame n` to pick a specific frame,
otherwise frame 0 is used. A `.json` document with caption `/apply` draws that whole layout
onto the image.

**Image**

| Command | Effect |
|---|---|
| `/start`, `/help` | Greeting / full command list + the main menu |
| `/blank [w h] [color]` | Start a blank canvas (default A4 @ 96 dpi, white) |
| `/url <link>` | Load an `http(s)` image |
| `/frame [n]` | Grab frame `n` of the loaded video (needs `ffmpeg` on `PATH`) |
| `/crop <spec> [album]` | Crop, e.g. `x1=10% x2=90% y1=10% y2=90%` |
| `/rotate [n]` | Rotate `n` quarter-turns clockwise (default 1) |
| `/filter <bw\|sepia\|none\|color>` | Black & white, sepia, clear, or a duotone tint |
| `/reset` · `/drop` | Clear pending edits (keep image) · forget the image entirely |
| `/image` · `/json` | Download the rendered result · download the layout JSON |
| `/status` | Show the working image, pending edits, pen and active project |

**Drawing / annotation** — coordinates are image pixels, or `x%,y%` of the image:

| Command | Effect |
|---|---|
| `/draw line x1,y1 x2,y2 …` | Draw a polyline (2+ points) |
| `/draw rect x1,y1 x2,y2` | Draw a rectangle (two opposite corners) |
| `/draw poly x1,y1 x2,y2 x3,y3 …` | Draw a closed polygon (3+ points) |
| `/color` · `/thickness` · `/markers` · `/style` · `/fill` | Set the pen (style for new lines) |
| `/pen` · `/undoline` · `/clearlines` | Show the pen · remove the last line · clear all lines |

**Server**

| Command | Effect |
|---|---|
| `/connect <url> [token]` · `/disconnect [url]` · `/connections` | Manage server connections |
| `/projects [url]` | List server projects as tappable buttons (tap to fetch) |
| `/fetch <name\|id>` | Load a server project as the working image |
| `/create [name]` | Publish the current result as a **new** server project |
| `/save` | Save the result + layout back to the active project (version-guarded) |

## Docker

A multi-stage [`Dockerfile`](Dockerfile) builds the Zig CLI (recompiling `core/`) and the
.NET bot into one runtime image with `ffmpeg`. Because it pulls in `core/` and `cli/`,
**build from the repo root** with `-f`, and pass the token at runtime (never baked in):

```bash
docker build -f bot/Dockerfile -t stencil-bot .
docker run --rm -e TELEGRAM_BOT_TOKEN=123456:ABC stencil-bot
# optional: persist working images, share sessions via Redis
docker run --rm -e TELEGRAM_BOT_TOKEN=123456:ABC -e REDIS_URL=redis://host:6379/0 \
  -v stencil-bot-data:/data stencil-bot
```

This makes the bot a standalone long-running service — no repo checkout or local CLI needed.

## BotFather assets

[`assets/`](assets/) holds the bot's branding, drawn from the shared Stencil icon family
(rounded violet panel + the signature yellow annotation polyline) with a paper-plane badge.
Telegram needs **raster** uploads, so only the ready-to-send images are kept:

| File | Use | Format / size |
|---|---|---|
| `assets/icon.png` | Bot profile picture (`/setuserpic`) | 512×512 PNG |
| `assets/description.jpg` | "What can this bot do?" photo | **640×360** JPEG |

Send `description.jpg` to @BotFather as a **Photo** (not a File) — the slot requires exactly
640×360.

## CI

The `bot` job in `.github/workflows/ci.yml` builds the solution and runs the offline test
suite on every push/PR. No token, Postgres, Redis or CLI binary is needed for the tests.
