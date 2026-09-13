# Stencil E2E

End-to-end **smoke** tests — one Node/Playwright harness that drives every user-facing
surface and black-boxes the collaboration server. Unlike the per-subproject unit suites,
these run the **real** artifacts: the browser app in a real Chromium, the unpacked MV3
extension, the Zig CLI binary, and the actual Go server over its REST/WS/TCP wire protocol.
How the harness is laid out: [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Running

```bash
cd e2e
npm ci
npx playwright install chromium        # add --with-deps on Linux CI

npm run test:ui                        # browser-app + extension — no Docker needed
E2E_STACK=1 npm test                   # all five projects (brings up db+redis+server via ../docker-compose.yml)
npm run test:stack                     # just fullstack + server-protocol
npm run report                         # open the HTML report after a run
```

| Project | Needs Docker stack? | What it drives |
|---|---|---|
| `browser-app` | no | Served `browser/` app via `window.stencil` |
| `browser-extension` | no | Unpacked `browser-extension/` in a persistent Chromium context |
| `fullstack` | **yes** | Browser app + real server (multi-client collaboration) |
| `server-protocol` | **yes** | REST + WS + TCP against the running server binary |
| `cli` | no | The Zig CLI binary (self-skips unless built / `STENCIL_CLI` set) |

The stack-dependent projects self-skip unless `E2E_STACK=1` is set. The `cli` project needs
the built binary — `(cd cli && zig build)` or point `STENCIL_CLI` at one. The static app
server is started by Playwright's `webServer` on `127.0.0.1:8188` (`helpers/config.js`), so a
stray `npm run serve` on `:8080` is never silently reused.

**Already have a server running?** Point the stack suites at it instead of starting compose:

```bash
E2E_STACK=1 E2E_SKIP_COMPOSE=1 npm test   # uses whatever is on SERVER_URL (default :8090)
```

The backing stack is deliberately left running between runs for fast iteration;
`E2E_STACK_DOWN=1` tears it down afterwards. A stack you left up — including one built from
an older server image — is silently reused by the next run, so after changing `server/` or
the compose env bring it down first: `docker compose down -v` from the repo root, or
`E2E_STACK_DOWN=1 E2E_STACK=1 npm test`.

**UI pins.** `tests/browser/ui-pins.spec.js` and `tests/browser-extension/ui-pins.spec.js` record
each UI state's computed styles and DOM shape and deep-equal them against
`pins/<platform>/<name>.json`; a failure names the element path and the property that moved.
Only `macos/` is recorded, so other platforms skip these specs. After an intended visual
change, re-record with `UPDATE_PINS=1 npm run test:ui`.

## Notes

- **Extension = headed Chromium.** MV3 extensions need a persistent context, so those suites
  launch their own with `headless: false` and `channel: 'chromium'`. In CI the job is wrapped
  in `xvfb-run`; locally a browser window opens briefly.
- **No wasm build needed.** The app runs its JS fallback when `js/wasm/` is absent (the built
  artifact is used if present).
- **State isolation.** `boot.js` clears `localStorage` per navigation and the config blocks
  the app service worker, so runs don't leak projects/servers between tests.
- **Server auth.** Token issuance is always admin-gated; the harness starts compose with
  `ADMIN_TOKEN` (default `e2e-admin`) and sends it as `X-Admin-Token`. With
  `E2E_SKIP_COMPOSE=1`, export `ADMIN_TOKEN` matching your server.
- **Wall times.** The non-stack run takes well under a minute; the full `E2E_STACK=1` run is
  a few minutes, because it goes single-file and `ws-peer-leave` waits out a real ~40 s
  ping/pong cycle.

## CI

The `e2e` job in `.github/workflows/ci.yml` builds the CLI, installs Chromium, brings up
`db redis server` with compose, and runs `xvfb-run -a npm test` with `E2E_STACK=1` — the
full run. The Playwright HTML report is uploaded as an artifact on failure.
