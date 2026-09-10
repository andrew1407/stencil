# Stencil E2E

End-to-end **smoke** tests — one platform-agnostic Node/Playwright harness that drives
every user-facing surface and black-boxes the collaboration server. This is the
*foundation*: one representative flow per surface, meant to grow.

Unlike the per-subproject unit suites (`node --test`, Doctest, `go test`, …), these tests
run the **real** artifacts: the browser app in a real Chromium, the unpacked MV3 extension,
and the actual Go server binary over its REST/WS/TCP wire protocol.

Like `mcp/`, `server/`, and `bot/`, this harness is an external adapter — it **never
compiles or links `core/`**, so it sits outside the parity contract in the repo `CLAUDE.md`.

## Architecture

```mermaid
graph TD
    subgraph E2E["e2e/ — Node/Playwright smoke harness (drives the real artifacts)"]
      HELP["helpers/ — static-server · boot · serverApi · wire (WS/TCP)"]
      TB["tests/browser — window.stencil + real pointer drawing"]
      TE["tests/extension — scan + hand-off + popup/side-panel UI"]
      TF["tests/fullstack — multi-client collaboration"]
      TS["tests/server — REST + WS + TCP"]
      TC["tests/cli — Zig binary black-box"]
    end
    WEB["Browser app"]
    EXT["Chrome extension (unpacked)"]
    SRV["Collaboration server binary"]
    CLI["Zig CLI binary"]
    STACK["docker compose — db + redis + server<br/><i>(only when E2E_STACK=1)</i>"]

    TB --> WEB
    TE --> EXT
    TF --> WEB
    TF --> SRV
    TS --> SRV
    TC --> CLI
    SRV -.-> STACK
```

> **Surface diagrams:** [browser](../browser/README.md#architecture) · [extension](../extension/README.md#architecture) · [server](../server/README.md#architecture) · [cli](../cli/README.md#architecture) — or the whole-system view in the [repository README](../README.md#architecture). Desktop (Qt) e2e lives
> elsewhere — see the [known gaps](#known-gaps-deliberate) below.

## Layout

```
helpers/
  static-server.js   tiny Node static server for browser/ (+ fixtures) — no python dep
  compose.js         globalSetup: docker compose up db+redis+server (only when E2E_STACK=1)
  compose.llm.yml    compose override applied by compose.js: server LLM env (stub key/model
                     + LLM_BASE_URL at the host's llm-stub) for the fullstack llm-proxy spec
  boot.js            gotoApp(page): navigate, clear state, await window.stencil
  serverApi.js       REST helpers (token issuance, project CRUD) over Playwright's request
  wire.js            WS (ws lib) + raw-TCP (net) clients for the live-edit protocol
  uiPin.js           UI regression pins: capture a subtree's computed styles + DOM shape
                     and deep-equal it against pins/<name>.json (UPDATE_PINS=1 re-records)
  llm-stub.js        scriptable stub LLM server (openai-compat / ollama / Anthropic
                     Messages wire shapes) — ALL model traffic in the suite ends here
pins/                recorded UI-pin baselines (one JSON per pinned state)
fixtures/            host pages (+ pixel.png / site.webmanifest) the extension scanner loads over http
                     (page-with-image.html · all-image-sources.html — one of every image reference)
tests/
  browser/   app.smoke     — window.stencil: blank/draw/rotate/crop, deep link
             editor        — real pointer drawing, undo/redo, crop tokens + px↔page, apply(), save→reload
             project-file  — .stencil open (image+layout+theme) + save→re-open round-trip via the facade
             chat          — AI assistant panel vs the stub LLM: §1 op-plan executes on the
                             facade, variant cards render, dock/float placement persists
             ctx-keyboard  — the canvas context menu walked with real key presses (↑/↓, → opens
                             a flyout, ← closes it, Enter picks, Escape) without panning the canvas
             ui-pins       — computed-style + DOM-shape pins for 15 editor states
                             (toolbar, modals, chat placements, fullscreen strip, toast)
             canvas-scrollbar — the canvas thumb is grey, and takes the accent only with the real
                             pointer on the bar's own strip (not on any hover of the canvas)
  extension/ handoff.smoke — scan images+CSS bg, new-tab AND in-page-modal hand-off, pin/unpin
             popup.smoke   — popup + side-panel UI: filter accordion, ⋯ menu + on-screen flyout, side-panel re-scan
             scan-sources  — every HTML/CSS image reference (img/srcset/input/svg/icons/meta + CSS) is scanned
             ui-pins       — the same pins for 6 extension states (popup list/filters/
                             row menu/assistant, side panel, options page)
             chat.smoke    — popup → AI chat page vs the stub LLM: `focus` highlights on the
                             page, `open` hands off `#stencil=` with the translated filter
  fullstack/ collab.smoke  — two clients + real server: create → cross-client visibility
             liveedit      — A auto-reloads a peer's server-side edit (live co-edit path)
             llm-proxy     — browser chat → server /llm/chat → stub Anthropic upstream
                             (x-api-key / anthropic-version headers, /llm/info)
  server/    rest.smoke, ws.smoke — REST lifecycle + hello→subscribe→welcome→edit/save/TCP
             events        — global /events feed (created/updated/deleted), peer-join, cursor relay
             files         — file-endpoint error paths + result-kind round-trip
             llm-rate-limit — /llm/chat spend controls: in-flight gate (held stub) +
                             per-session burst bucket, both → 429 rateLimited
             chat-file-lifecycle — contract-§12 `chat` file kind: PUT → GET round-trip →
                             DELETE → 404 (filestore-only; original stays undeletable)
             ws-peer-leave — WS keepalive reaps a half-open peer (~40s); peer-leave
                             reaches the survivor (deliberately slow)
  cli/       pipeline      — Zig binary black-box: blank/rotate/crop/filter/layout/URL-input,
                             ext auto-fill, error contract; asserts the written PNG's IHDR dims.
                             Also opens the SAME fixtures/project.stencil the browser spec opens
                             (cross-surface parity) + bundles an image back into a .stencil
             llm-prompt    — /prompt vs the stub LLM: piped --console session, a §1
                             op-plan executes for real (saved PNG dims swap)
```

### Known gaps (deliberate)

- **peer-leave is detected, but only within ~40s.** The server now runs a WS keepalive
  (30s ping + 10s pong timeout — `server/internal/transport/ws.go`), so a half-open peer
  is reaped and `peer-leave` reaches the survivors within that window. Asserted end-to-end
  by `tests/server/ws-peer-leave.spec.js` (deliberately slow: it waits out a real
  ping/pong-timeout cycle). Departure is still not instant — presence can lag a dead peer
  by up to ~40s.
- CLI, MCP, and bot have no e2e here; no visual-regression baselines. (The CLI *is* driven
  end-to-end by the `cli` project above; MCP/bot are not.)
- **Desktop (Qt) e2e lives elsewhere, on purpose.** The desktop app is a native Qt binary, not
  a wire-protocol surface this Node harness can drive, so its end-to-end test is a QtTest target
  built with the desktop CMake project: [`../desktop/tests/mainWindow.gui.cpp`](../desktop/tests/mainWindow.gui.cpp)
  (run via `ctest --test-dir desktop/build`). It drives the real `MainWindow` offscreen.

## Test projects

| Project | Needs Docker stack? | What it drives |
|---|---|---|
| `browser-app` | no | Served `browser/` app via `window.stencil` |
| `extension` | no | Unpacked `extension/` in a persistent Chromium context |
| `fullstack` | **yes** | Browser app + real server (multi-client collaboration) |
| `server-protocol` | **yes** | REST + WS + TCP against the running server binary |
| `cli` | no | The Zig CLI binary (self-skips unless built / `STENCIL_CLI` set) |

The stack-dependent projects **self-skip** unless `E2E_STACK=1` is set. The `cli` project
needs the built binary — `(cd cli && zig build)` or point `STENCIL_CLI` at one.

## Running

```bash
cd e2e
npm install
npx playwright install chromium        # add --with-deps on Linux CI

# UI surfaces only — no Docker needed:
npm run test:ui                        # browser-app + extension

# Full stack (brings up db+redis+server via ../docker-compose.yml):
E2E_STACK=1 npm test                   # all four projects
npm run test:stack                     # just fullstack + server-protocol

npm run report                         # open the HTML report after a run
```

The static app server is started automatically by Playwright's `webServer` on a
dedicated port (`127.0.0.1:8188`, see `helpers/config.js`) so a stray `npm run serve`
on `:8080` is never silently reused. The backing stack is left running between runs for
fast iteration; tear it down with `docker compose down -v` (repo root), or set
`E2E_STACK_DOWN=1` to have the run do it.

**Already have a server running?** Point the stack suites at it instead of starting
compose:

```bash
E2E_STACK=1 E2E_SKIP_COMPOSE=1 npm test   # uses whatever is on SERVER_URL (default :8090)
```

## Notes & gotchas

- **Extension = headed Chromium.** MV3 extensions require a persistent context and load
  most reliably headed, so the `extension` project launches with `headless: false`. In CI
  the job is wrapped in `xvfb-run`; locally on macOS/Windows a browser window opens briefly.
  It uses `channel: 'chromium'`, so `npx playwright install chromium` is required.
- **No wasm build needed.** The app runs its JS fallback when `js/wasm/` is absent and is
  behaviorally identical for these flows (the committed wasm artifact is used if present).
- **State isolation.** `boot.js` clears `localStorage` per navigation and the config blocks
  the app service worker, so runs don't leak projects/servers between tests.
- **Server auth.** Token issuance is always admin-gated (an unset `ADMIN_TOKEN` makes the
  server generate a per-boot token, printed once). The harness starts compose with
  `ADMIN_TOKEN` (default `e2e-admin`; override via env) and `helpers/serverApi.js` sends it
  as `X-Admin-Token`. With `E2E_SKIP_COMPOSE=1`, export `ADMIN_TOKEN` matching your server.
  Side effect: with an admin token always set, the server's LLM proxy enables whenever the
  stub provider is configured, so the fullstack llm-proxy spec runs instead of self-skipping.
- **UI pins.** `tests/browser/ui-pins.spec.js` + `tests/extension/ui-pins.spec.js` record
  each UI state's computed styles and DOM shape (`helpers/uiPin.js`) and deep-equal them
  against `pins/<name>.json` — no screenshots; a failure names the element path and the
  property that moved. They freeze the app's own motion (`motionMode 'none'` /
  `StencilMotion.set('none')` + emulated `prefers-reduced-motion`) and pin the light theme.
  After an INTENDED visual change, re-record: `UPDATE_PINS=1 npm run test:ui`.

## CI

The `e2e` job in `.github/workflows/ci.yml` runs this harness on push/PR to `main`,
parallel to the nine unit jobs: `npm install` → `playwright install --with-deps chromium`
→ `docker compose up -d --wait db redis server` → `xvfb-run -a npm test` with `E2E_STACK=1`.
The Playwright HTML report is uploaded as an artifact on failure.
