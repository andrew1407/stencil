# Stencil — Telegram bot (.NET)

A Telegram bot that drives Stencil's image pipeline from a chat: upload a photo (or start a
blank canvas), crop / rotate / filter / draw a layout onto it, and download the result image
or its layout JSON — and connect to a Stencil [collaboration server](../server/README.md) to
list, fetch, create and save shared projects. It shells out to the Zig CLI for every pixel
transform, so results match the other editors. For the project overview see the
[repository README](../README.md); for how the bot is built, [`ARCHITECTURE.md`](ARCHITECTURE.md);
for the chat with its buttons, [`usecases/bot/USECASES.md`](../usecases/bot/USECASES.md).

Live instance: [@stencil_editor_bot](https://t.me/stencil_editor_bot).

```
You: (send a photo)
Bot: result.png — 1280x720    [🔄 Rotate] [B&W] [Sepia] [♻︎ Reset] [📄 JSON] …
You: /crop x1=10% x2=90% y1=10% y2=90%
You: /color #ff5623   /thickness 4
You: /draw rect 20%,20% 80%,80%   → annotates the image
You: /json            → downloads the layout JSON
You: /script @crop 10% ; @filter bw   (or upload a .stc file)
You: /connect http://localhost:8090
You: /create Shared   → publishes the result as a new server project
```

## Build · test · run

Needs the **.NET 10** SDK and the built CLI (`cd cli && zig build`). Video frames and LLM
image downscaling additionally use `ffmpeg` on `PATH` (optional).

```bash
# from bot/
dotnet build Stencil.TelegramBot.slnx
dotnet test  Stencil.TelegramBot.slnx              # offline — no token, server, CLI, LLM or Redis needed
dotnet test  Stencil.TelegramBot.slnx --filter Category=Bench   # opt-in benchmarks
dotnet run --project src/Stencil.TelegramBot.Bot   # run the bot (needs TELEGRAM_BOT_TOKEN + the CLI)
```

`dotnet restore` fills a repo-local `bot/packages/` folder (gitignored), never `~/.nuget`;
commit the regenerated `packages.lock.json` whenever you change a `<PackageReference>`.

### Docker

The multi-stage [`Dockerfile`](Dockerfile) builds the Zig CLI and the .NET bot into one
runtime image with `ffmpeg`. Build **from the repo root** (it pulls in `core/` and `cli/`)
and pass the token at runtime:

```bash
docker build -f bot/Dockerfile -t stencil-bot .
docker run --rm -e TELEGRAM_BOT_TOKEN=123456:ABC stencil-bot
# optional: persist working images, share sessions via Redis
docker run --rm -e TELEGRAM_BOT_TOKEN=123456:ABC -e REDIS_URL=redis://host:6379/0 \
  -v stencil-bot-data:/data stencil-bot
```

## Configuration

Copy the template and set at least the token and the allowlist:

```bash
cp bot/.env.example bot/.env   # then paste your @BotFather token into TELEGRAM_BOT_TOKEN
```

Real environment variables always win over `.env`; the real `bot/.env` is gitignored.

| Variable | Default | Purpose |
|---|---|---|
| `TELEGRAM_BOT_TOKEN` | — (**required**) | Bot token from @BotFather |
| `STENCIL_BOT_ALLOWED_USERS` | empty (**required**) | Comma-separated Telegram user ids allowed to use the bot — see [the allowlist](#the-bot-is-opt-in-per-user). Empty = the bot is off for everyone |
| `STENCIL_CLI` | auto-discovered | Path to the `stencil` CLI binary |
| `REDIS_URL` | — (in-memory) | Redis for per-user session state: `redis://[user:password@]host[:port][/db]` (`rediss://` for TLS) or StackExchange's `host:port[,option=value]` |
| `STENCIL_BOT_DATA_DIR` | `<temp>/stencil-bot` | Scratch dir for working images |
| `STENCIL_TLS_INSECURE` | `false` | Accept self-signed certs for `https` servers (dev) |
| `STENCIL_BOT_BROWSER_URL` | `http://localhost:8080` | Base URL of the **served browser app**; `/link` builds its desktop hand-off through that app's `launch.html`. A bot link is tapped on someone else's machine, so set a public address |
| `STENCIL_BOT_MAX_CONCURRENT_CLI` | CPU count | Cap on concurrent CLI processes |
| `STENCIL_BOT_MAX_CONCURRENT_LLM` | `8` | Cap on concurrent LLM calls; full ⇒ an immediate "busy" reply; `0` = unlimited |
| `STENCIL_BOT_HTTP_TIMEOUT_SECONDS` | `30` | Per-request timeout for server REST calls |
| `STENCIL_BOT_MAX_DOWNLOAD_MB` | `50` | Max size of a Telegram photo/video download (a `.json` / `.stencil` document caps at 4 MB) |
| `STENCIL_BOT_CLI_TIMEOUT_SECONDS` | `120` | Wall-clock limit on one CLI run before it is killed |
| `STENCIL_BOT_WORKSPACE_TTL_MINUTES` | `60` | Age after which orphaned scratch files are swept |

**AI assistant** (`/prompt`, or `/chat` for hands-free chat mode) — the same `STENCIL_LLM_*`
keys as every other surface; how to get a model running behind them is in the
[root README](../README.md#ai-assistant--setting-up-a-model):

| Variable | Default | Purpose |
|---|---|---|
| `STENCIL_LLM_PROVIDER` | `ollama` | `ollama` \| `openai-compat` \| `stencil-server` |
| `STENCIL_LLM_BASE_URL` | `http://localhost:11434` (ollama) / `http://localhost:1234/v1` (openai-compat) | Endpoint origin for the local providers |
| `STENCIL_LLM_MODEL` | empty | Model name (empty = provider/server default) |
| `STENCIL_LLM_API_KEY` | empty | Sent as `Authorization: Bearer` on `openai-compat` only |
| `STENCIL_LLM_SERVER_URL` | empty | `stencil-server` only: which collaboration server proxies Anthropic; unset = the user's first `/connect`-ed server |
| `STENCIL_LLM_SERVER_TOKEN` | empty | The operator's bearer for a pinned `STENCIL_LLM_SERVER_URL`, used for users who have not `/connect`-ed to it themselves |
| `STENCIL_LLM_PROFILES` | empty | Named chat-API profiles users pick with `/chatapi` (see [`.env.example`](.env.example)) |

Attached images larger than 1568 px on the long edge are downscaled through `ffmpeg` before
being sent; without it, images up to 8 MB are attached as-is and larger ones are skipped.

### The bot is opt-in per user

`STENCIL_BOT_ALLOWED_USERS` gates every command, button and upload except `/start` and
`/help`, and it **fails closed**: with the list unset the bot answers nobody. Every command
costs the operator something — `/url` fetches, every edit forks a CLI process, each `/prompt`
spends the configured API key. An unlisted caller gets one plain sentence; the id to add is
logged server-side. Send `/start` to the bot to learn your id.

## Chat surface

Every slash command has a matching inline **button**. A command that needs arguments, sent
bare, replies with its possible values instead of failing. Replies open with a tone glyph:
**🔴** it didn't happen, **🟡** it did with a caveat, **✅** a confirmed action, **ℹ️** a
notice.

**Sources** — set the working image by sending a **photo** or **image file**, or by pasting
an **image link**. Add a **caption command** to apply it immediately (`/crop …`, `/filter bw`,
`/draw rect 20%,20% 80%,80%`); while `/chat` mode is on, a plain-text caption is a `/prompt`
about that photo. Send a **video** to grab a frame (caption `/frame n` to pick one). A
`.json` document captioned `/apply` draws that layout; a **`.stencil` project file** opens a
whole project, and `/project` downloads the current one as a portable `.stencil`. A **`.stc`
script file** runs as a script the moment it arrives — the file form of `/script`.

**Albums** — a multi-photo message runs its caption once per photo, in order, and sends the
results back as one album; the last photo's result becomes the working image.

<!-- generated from src/Stencil.TelegramBot.Bot/Assets/botCommands.json — rewrite with `BOT_UPDATE_PROSE=1 dotnet test` -->
**Image**

| Command | Effect |
|---|---|
| `/start`, `/help` | Greeting / full command list + the main menu |
| `/prompt <request>` (alias `/p`) | **Ask the AI assistant** to plan and apply edits — e.g. `/prompt make it sepia and crop 10% off each side`, or `/prompt give me 3 variants: rotated, tinted red, contoured`. The reply's validated op-plan (see [`llm-contract.md`](../contracts/llm/llm-contract.md)) executes through the **same** editing service as the slash commands, so `/undo` etc. work on AI edits; the working image rides along as the vision attachment (a photo captioned `/prompt …`, or `/prompt` sent as a reply to a photo, targets that photo). One updated result comes back with the edit menu; variants arrive as a media album. A spinning **◐ Working on your request…** notice goes up the moment the turn starts (a vision plan over a big image can take a minute) and is deleted when the reply — or the error — lands. A turn that never delivered — it failed (timeout, unreachable endpoint, a truncated reply) or you ended it with the notice's **⏹ Stop** button — comes back with a **🔄 Retry** button that re-runs the same request, so recovering costs one tap instead of retyping it on a phone; a *refusal* has no button, since re-sending it verbatim would only repeat it |
| `/script <script>` (alias `/stc`) | **Run a Stencil script** — the `.stc` language (see [`contracts/stc/`](../contracts/stc/)), one-line or multi-line: `/script @crop 10% ; @filter bw ; @line (0,0) (100%,100%)`. The bot hands the script to the CLI's `--script-plan` mode, which lowers it to the same validated op plan `/prompt` produces, and every op then runs through the **same** editing service as the slash commands — so `/undo` walks a script's edits back one at a time. Lengths in `%` resolve against the frame you are looking at. **Uploading a `.stc` file is the file form of this command** — send it as a document and it runs the moment it arrives; there is no separate `/script-run`. A block header `@source <link>:` loads its own http(s) image (several blocks come back as a media album); a `@source` naming a local path, directory or glob is refused, because the bot has no file system of yours to read. `@save` goes to the active server project, the same place `/save` writes. A script with an error runs **nothing** — the reply names the line, column and error code |
| `/chatapi [name]` | **Pick which chat API the assistant uses** — bare, it lists the ones the operator configured (`STENCIL_LLM_PROFILES`, see [`.env.example`](.env.example)) with a button each and a ✅ on the current one; `/chatapi llama` selects by name. The choice is **per user** and lives in the session, so one person trying a local model never moves anyone else, and it survives restarts. It is a picker, not free text: provider, base URL, model and token all come from the operator's environment, never from a chat message — a bot that took a URL from a message would issue requests to whatever host that message named. With no profiles configured the bot has exactly one chat API and says so |
| `/chat [on\|off\|clear]` | **Chat mode** — keep talking to the assistant without retyping `/prompt`. While it is on, every plain message you send is handled exactly like `/prompt <that text>` (same op-plan execution, same edit menu, variants as a media album). Started by `/chat` (or the **💬 Chat with assistant** button on the main menu / the **💬 Chat** button on the edit menu), stopped by `/chat off` or the **🚪 Chat off** button that rides the confirmation. `/chat clear` (or the **🧹 Clear chat** button next to it) makes the assistant forget the conversation so far without leaving chat mode; `/drop` clears it too. Precedence is strict: slash commands are never swallowed, an in-progress free-text flow (Rename / Describe / custom expiry) answers first, and a bare image link still loads like `/url` — only what's left over goes to the assistant. The flag lives in the per-user session (`UserSession.ChatMode`), so it survives across messages and shows in `/status`. `/chat save on\|off` (or the **💾 Save chats** toggle on the chat menu) is the contract-§12 **chat persistence** opt-in (default off, `UserSession.SaveChats`): with it on, after each assistant turn the conversation (text only, displayed replies, ≤ 32 messages — never images) is uploaded to the active **server** project's `chat` file kind and restored (history reseeded, "Restored N…" noted) when that project is `/fetch`ed again; `/chat clear` then also deletes the server copy. The bot keeps no local chat store, so without an active server project nothing is written |
| `/blank [format] [w h] [color]` | Start a blank canvas: a named ISO format (e.g. `b5`) **or** pixel dims (default A4 @ 96 dpi, white) |
| `/format [name\|custom w h]` | Set the page format (A0–C10, case-insensitive, or custom cm dims) — the `/blank` default page (custom cm dims convert to pixels at 96 dpi, like the CLI console), written into the saved layout's `pageSize`; bare lists all 33 formats |
| `/url <link>` | Load an `http(s)` image |
| `/sourcesite <link> [count] [filter=…] [format=…] [minw/maxw/minh/maxh=…] [group=N]` | **Scrape a web page's media** into the chat: the CLI fetches the page, extracts + filters its `<img>`/`<video>`/`poster`/CSS-background URLs and downloads the matches (`--source-site` mode — HTML parsing is the CLI's job, not `core/`). Each measured image comes back as a photo, each video/unmeasured item as a document, plus a summary. Bare integer = count (**default 5**; `0` = all); `filter=` category tokens (`img\|video\|background\|poster`), `format=` extension tokens (`png\|jpg\|…`), `min/max` inclusive px bounds, `group=` a 0-based page. The link is SSRF-vetted like `/url` |
| `/sourceupload <link> [index=0] [format=…] [minw/maxw/minh/maxh=…]` | **Scrape a page and load ONE image to edit** — the chat analog of the console `/source-upload`. Isolates the still at 0-based `index` (image-category only: `img\|background\|poster`, video excluded) via a one-item scrape, adopts it as the **editable** working image (replacing the session, like `/url`), then renders + sends it with the edit menu. Bare integer = the index; `format=`/`min/max` filter the candidate stills. Replies `No image at index N` when nothing lives there. The link is SSRF-vetted like `/url` |
| `/frame [n]` | Grab frame `n` of the loaded video (needs `ffmpeg` on `PATH`) |
| `/crop <spec> [album]` | Crop, e.g. `x1=10% x2=90% y1=10% y2=90%` |
| `/rotate <n>` | Rotate `n` quarter-turns clockwise (bare lists the variants: `1`, `2`, `-1`) |
| `/filter <bw\|sepia\|invert\|contour\|none\|color>` | Black & white, sepia, invert, edge-detect contour, clear, or a duotone tint |
| `/reset` · `/drop` | Clear pending edits (keep image) · forget the image entirely — `/drop` is a full start-over, so it also clears the assistant's conversation |
| `/layout <json \| link>` | Apply a layout: inline JSON or an `http(s)` link to a layout `.json` (same validation as uploading the file; links are SSRF-vetted like `/url`) |
| `/image` · `/json` | Download the rendered result · download the layout JSON |
| `/status` | Show the working image, pending edits, pen and active project |

**Drawing / annotation** — coordinates are image pixels, or `x%,y%` of the image:

| Command | Effect |
|---|---|
| `/draw line x1,y1 x2,y2 …` | Draw a polyline (2+ points) |
| `/draw rect x1,y1 x2,y2` | Draw a rectangle (two opposite corners) |
| `/draw poly x1,y1 x2,y2 x3,y3 …` | Draw a closed polygon (3+ points) |
| `/color` · `/thickness` · `/points` · `/style` · `/fill` | Set the pen (style for new lines) |
| `/pen` · `/undoline` · `/clearlines` | Show the pen · remove the last line · clear all lines |

**Server**

| Command | Effect |
|---|---|
| `/connect <url> [token]` · `/disconnect [url]` · `/connections [admin\|session]` | Manage server connections. The URL may be an **invite link** (`<url>#token=<tok>`) — its fragment supplies the token; a token argument still wins over it. Each connection remembers what its credential turned out to be (browser parity: an **admin** token can't list projects but mints session tokens, and is proven once a mint-then-validate round succeeds); `/connections` marks those `[admin]` and takes an optional `admin` / `session` filter — tokens themselves are never printed |
| `/projects [url]` | List server projects as tappable buttons (tap to fetch) |
| `/fetch <name\|id>` | Load a server project as the working image |
| `/create [name]` | Publish the current result as a **new** server project |
| `/save` | Save the result + layout back to the active project (version-guarded) |
| `/link` (`/desktop`, `/open-in`) | **Outbound deep link** — the reverse of `/start`: an `https` link (also the 🔗 Link button on the `/status` and edit menus) that opens the active project in the **desktop app**. Chat apps only linkify `http(s)`, so it points at the browser app's `launch.html` bounce page, which forwards to `stencil://open?server=…&id=…&version=…`. Server projects only (a link carries a reference, not image bytes) and **no token rides it** — whoever follows it connects with their own credential. The bounce page comes from `STENCIL_BOT_BROWSER_URL` |
| `/expire <n unit \| never>` | Set the active project's expiry (version-guarded) — bare `/expire` (or the ⏳ Expiration button in `/status`) opens a duration picker: **1 day · 3 days · 1 week · Fortnight · 1 month · 3 months · Custom · Never**; **Custom** awaits a free-text span like `3 days`, `week 4`, `2 weeks`, `1 month` |
| `/start <payload>` | Inbound deep link: t.me `?start=` payloads from the browser/desktop **"Open in… → Telegram"** button decode to (server, project id); the bot connects like a fresh client (token minted via `POST /auth/token`) and fetches the project into the chat. Failures reply with the manual `/connect` + `/fetch` recipe |
<!-- /generated -->

## BotFather assets

[`assets/`](assets/) holds the bot's branding. Telegram needs raster uploads:

| File | Use | Format / size |
|---|---|---|
| `assets/icon.png` | Bot profile picture (`/setuserpic`) | 512×512 PNG |
| `assets/description.jpg` | "What can this bot do?" photo | **640×360** JPEG |

Send `description.jpg` to @BotFather as a **Photo** (not a File) — the slot requires exactly
640×360.
