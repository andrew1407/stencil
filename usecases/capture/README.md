# Capturing the use-case images

Every PNG and GIF under `usecases/<app>/img/` comes out of one script here, driving the real
app in-process: Playwright for the browser app, the extension and Telegram Web; a Qt binary
grabbing its own window for the desktop; VS Code over its Chromium debug port, for the
extension and as the real terminal the CLI console is photographed in; a small renderer that
turns the CLI's pinned coloured listings into pages. Nothing here is a dependency of any app
— Playwright is borrowed from `e2e/node_modules`, GIFs come from the system `ffmpeg`.

## Prerequisites

- `cd e2e && npm ci && npx playwright install chromium`
- `ffmpeg` on the PATH
- the CLI built: `cd cli && zig build`
- the desktop configured once with the capture target: `cmake -S desktop -B desktop/build`
  (the script adds `-DSTENCIL_DOCS_CAPTURE=ON` and builds `stencil_docs_capture`)
- VS Code installed (path per OS in `config/shared.json`; `STENCIL_VSCODE` overrides it)

## One command per app

```bash
node usecases/capture/browser.mjs             # browser app
node usecases/capture/browserExtension.mjs    # Chrome extension (headed; a Chromium window opens)
python3 usecases/capture/cli_listings.py      # the pinned listings, as HTML
node usecases/capture/cliRender.mjs           # …photographed
node usecases/capture/cliTerminal.mjs         # the console in VS Code's terminal
node usecases/capture/desktop.mjs             # builds and runs the Qt capture binary, offscreen
node usecases/capture/vscodeExtension.mjs     # one VS Code launch per theme; ~1 min
node usecases/capture/bot.mjs                 # see below
./usecases/capture/runAll.sh                  # all of the above but the bot
usecases\capture\runAll.bat                   # the same, on Windows
```

Add `--only <name-prefix>` to the Node scripts to redo one shot while iterating.

## What the shots are made of

`config/shared.json` holds what every surface shares — the repo URLs the canvases are loaded
from, the collaboration server, the prompts, the stub's op-plans, the layout and script the
shots draw, the image budget, the GIF look and where VS Code lives on each OS.
`config/<app>.json` holds that app's own: viewports, timeouts, clip lengths and its themes.

A shot's theme comes from its name first — anything ending in `-light` / `-dark` is one half
of a documented pair — then from `theme.steps[<name>]`, then from the app's `theme.mode`:

| mode | means |
|---|---|
| `dark` | the default on the browser app, the desktop and both extensions |
| `light` | named per shot in `theme.steps`, for the handful that document the light palette |
| `system` | resolved through `theme.systemPrefers` |
| `random` | resolved per shot from `theme.randomSeed`, so a re-run reproduces it |

Steps are tables (`browser/stillSteps.mjs`, `browser/clipSteps.mjs`, and the `STEPS` array in
each other script): a name, and what to do. Waits are for the state being photographed — a
finished animation, a decoded canvas, a terminal buffer that stopped changing — not a guessed
delay; the few remaining pauses are clip timings and live in the config.

## The bot

Telegram has to be logged into by a person, once, in a dedicated profile kept outside the
repo (`~/.stencil-docs-telegram`; `--profile` moves it). The script opens Telegram Web, waits
for that login, then talks to the bot named by `--bot` (the live `@stencil_editor_bot` by
default, or one you run locally with `dotnet run` and your account on its allowlist) and
screenshots the chat column. The bot is never started from here and its env is never read.

## Rules

- Outputs have fixed names and are overwritten in place; intermediates go to `.out/`
  (gitignored). A script exits non-zero when a shot is missing or over the size budget
  (PNG ≤ 512 KB, GIF ≤ 4 MB), so nothing heavy lands in git. GIFs are 1000 px wide on a
  256-colour palette; a clip that would not fit gets its own `width`/`fps` in its config.
- Canvases use the apps' own blank pages; "open from a link" flows load the repository's own
  files from `raw.githubusercontent.com`.
- The assistant shots use a real model when a collaboration server with an LLM proxy is
  reachable: set `STENCIL_DOCS_SERVER_URL` (default `http://127.0.0.1:8090`) and either
  `STENCIL_DOCS_SERVER_TOKEN` or a session token in `.out/session.token` (mint one with
  `POST /auth/token` and the server's admin token). The CLI and bot captures need the same
  token in their `STENCIL_LLM_*` env (`bot.mjs` never starts the bot). Without a token the
  browser, extension and desktop fall back to the e2e LLM stub's canned answers, and the CLI
  and bot skip their prompt shots.
- **No shot carries anything personal.** The VS Code captures run out of a neutral root
  (`vscode.roots` in `config/shared.json`, `/tmp/stencil-capture` by default) with a copy of
  the CLI beside them and a shell started without rc files, so a terminal shows `$ ` and
  `/tmp/...` paths — never a user name, a host name or a home directory. The Telegram shots
  clip to the message column at a message boundary, so no half-painted bubble or sidebar
  reaches an image.
- Keep a capture in step with its app: a moved control or a renamed id is fixed here, never
  by editing an image.
