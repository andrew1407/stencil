# Capturing the use-case images

Every PNG and GIF under `usecases/docs/<app>/img/` comes out of one script here, driving the real
app in-process: Playwright for the browser app, the extension and Telegram Web; a Qt binary
grabbing its own window for the desktop; VS Code over its Chromium debug port, for the
extension; a pseudo-terminal running the real CLI and pystencil, replayed through a small
renderer that also turns the CLI's pinned coloured listings into pages. Nothing here is a
dependency of any app — Playwright is borrowed from `e2e/node_modules`, GIFs come from the
system `ffmpeg`.

## Prerequisites

- `cd e2e && npm ci && npx playwright install chromium`
- `ffmpeg` on the PATH
- the CLI built: `cd cli && zig build`
- the desktop configured once with the capture target: `cmake -S desktop -B desktop/build`
  (the script adds `-DSTENCIL_DOCS_CAPTURE=ON` and builds `stencil_docs_capture`)
- VS Code installed (path per OS in `config/shared.json`; `STENCIL_VSCODE` overrides it)
- for the server shots, either a collaboration server at the configured URL or `go`, `initdb`
  and `pg_ctl` on the PATH, so a throwaway one can be built (see below)

## One command per app

```bash
node usecases/capture-runner/browser.mjs             # browser app (headless)
node usecases/capture-runner/browserExtension.mjs    # Chrome extension (headed window)
python3 usecases/capture-runner/cli_listings.py      # the pinned listings, as HTML
node usecases/capture-runner/cliRender.mjs           # …photographed
node usecases/capture-runner/cliTerminal.mjs         # the CLI's and pystencil's terminal scenes, headless
node usecases/capture-runner/desktop.mjs             # builds+runs the Qt binary, offscreen
node usecases/capture-runner/vscodeExtension.mjs     # one VS Code launch per theme; ~1 min
node usecases/capture-runner/bot.mjs                 # see below
python3 usecases/capture-runner/main.py              # all of the above but the bot
```

Add `--only <prefix>[,<prefix>…]` to the Node scripts to redo some shots while iterating.

## What the shots are made of

`config/shared.json` holds what every surface shares — the repo URLs the canvases are loaded
from, the collaboration server, the prompts, the stub's op-plans, the layout and script the
shots draw, the image budget, the GIF look and where VS Code lives on each OS.
`config/<app>.json` holds that app's own: viewports, timeouts, clip lengths and its themes.

A shot's theme comes from its name first — anything ending in `-light` / `-dark` is one half
of a documented pair — then from `theme.steps[<name>]`, then from the app's `theme.mode`. Every
app documents its light palette through its pairs alone, so `theme.steps` stays empty:

| mode | means |
|---|---|
| `dark` | the default on the browser app, the desktop and both extensions |
| `light` | a single shot named in `theme.steps` |
| `system` | resolved through `theme.systemPrefers` |
| `random` | resolved per shot from `theme.randomSeed`, so a re-run reproduces it |

Steps are tables (`browser/*Steps.mjs`, `extension/*Steps.mjs`, `vscode/*Steps.mjs`, and the
`STEPS` array in each other script): a name, and what to do. Waits are for the state being
photographed — a finished animation, a decoded canvas, a terminal buffer that stopped changing —
not a guessed delay; the few remaining pauses are clip timings and live in the config.

## Clips

A clip is a run of lossless PNG frames: a screencast of the page for the browser app and the
extension (`lib/shot/cast.mjs`), the window's own grabs for the desktop, screenshots for VS Code
and the terminal. No video codec sits between the app and the GIF. Pages get a drawn pointer
(`lib/shot/pointer.mjs`) — an arrow with a ring on each press, a fingertip for touch — because a
screencast paints no cursor; the Qt capture paints its own into each frame. Identical frames
merge, and every GIF opens on `gif.firstMs` of its first picture and rests `gif.lastMs` on its
last (`config/shared.json`), so a reader always has time to see the result.

## The collaboration server

The server shots connect to the URL `server.url` names (`config/<app>.json` may give an app its
own). When something answers there it is used as it is: tokens are minted with
`STENCIL_DOCS_ADMIN_TOKEN`, or with the admin token a local server of this runner left behind,
else `STENCIL_DOCS_SERVER_TOKEN` is used as the one session token. When nothing answers,
`lib/collab.mjs` builds a throwaway server from this checkout: a fresh Postgres cluster under
`.out/collab-<port>/` on `server.localDbPort`, the in-process event bus and a per-run admin token,
all on loopback, stopped when the script ends. A capture that seeds a server project for its shots
removes it again when it is done.

## The bot

Telegram has to be logged into by a person, once, in a dedicated profile kept outside the
repo (`~/.stencil-docs-telegram`; `--profile` moves it). The script opens Telegram Web, waits
for that login, then talks to the bot (the live one, or one you run locally with `dotnet run`
and your account on its allowlist) and screenshots the chat column. The bot is named by
`--bot <username>`, else `STENCIL_DOCS_BOT`, else `STENCIL_BOT_NAME` — from the environment,
or the one line of `bot/.env` that sets it. The bot is never started from here, and its token is
never read or used.

The files it uploads (a `.stc`, a `.json` layout, a `.stencil` the CLI writes, a short clip) are
made fresh under `.out/bot-samples/`. Two groups of shots wait to be asked for, and without them
the images already there stay: `--with-model` (or `STENCIL_DOCS_BOT_MODEL=1`) when the bot has a
model, for `prompt` and `chat-turn`; `--with-server` for the server shots and the Open In…
arrival, against `config/shared.json`'s server (a local bot that allows private servers reaches
it) or `--server <url>` with `STENCIL_DOCS_BOT_SERVER_TOKEN` in the env.

## Rules

- Outputs have fixed names and are overwritten in place; intermediates go to `.out/`
  (gitignored). A script exits non-zero when a shot is missing or over the size budget
  (PNG ≤ 512 KB, GIF ≤ 4 MB), so nothing heavy lands in git. Stills are taken at 2× and put on a
  256-colour palette; GIFs are 1000 px wide, a side-by-side clip wider, on the same palette.
- Canvases use the apps' own blank pages; "open from a link" flows load the repository's own
  files from `raw.githubusercontent.com`.
- The video shots open a clip `lib/sampleMedia.mjs` makes with ffmpeg, written under the
  neutral root (the desktop dialog shows a chosen file's full path) and served for the URL
  tab on `shared.json` `mediaPort`. Nothing binary is committed. Two things constrain them:
  Qt Multimedia returns no frame under the offscreen platform, so `config/desktop.json`
  `videoShots` take their own pass on the real one; and the desktop hands a URL straight to
  `QMediaPlayer` (`MediaLoaderVideo.cpp`), which streams it through the platform media stack
  and refuses that little server — so `open-video-url` is browser-only and self-skips there.
  `config/desktop.json` sets its own `mediaPort`, so the browser and desktop captures can run
  side by side.
- The assistant shots run on the e2e LLM stub, whose canned plan frames the S of the repository
  picture and warms it with sepia. A real model answers only with `STENCIL_DOCS_REAL_LLM=1` and a
  server token whose server proxies one; a caption never claims a real model unless the shot came
  from one. The settings shots point at a local Ollama: a stand-in answers its version probe on
  `ollama.port` unless a real one already does. The bot takes its prompt shots only with
  `--with-model`.
- **No shot carries anything personal.** The VS Code captures run out of a neutral root
  (`vscode.roots` in `config/shared.json`, `/tmp/stencil-capture` by default) with a copy of
  the CLI beside them and a shell started without rc files, so a terminal shows `$ ` and
  `/tmp/...` paths — never a user name, a host name or a home directory. The terminal scenes run
  in `/tmp/stencil-term/<app>` with a bare `$ ` shell, and stub `open` / `xdg-open` commands record
  the CLI's hand-offs instead of opening windows. The Telegram shots
  clip to the message column at a message boundary, so no half-painted bubble or sidebar
  reaches an image.
- Easter eggs stay secret: no shot or walkthrough shows a logo show, a skin, a secret word or
  CLI mode.
- Keep a capture in step with its app: a moved control or a renamed id is fixed here, never
  by editing an image.
