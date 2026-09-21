# Browser app architecture

The system-wide design — the parity contract, canonical data, the layer model, the pattern vocabulary — is in the root [`ARCHITECTURE.md`](../ARCHITECTURE.md).

The browser app is the reference front-end: a vanilla ES-module editor with no build step,
running `core/` as wasm behind a JS fallback that matches it op-for-op. It owns the canonical
data tables in `js/config/`, the `.stencil` document format and the layout payload the other
surfaces mirror. It does not own the collaboration protocol (`server/internal/protocol`) and
carries no codec beyond what the DOM provides.

```mermaid
graph TD
    CORE["core/"]
    WASM["js/wasm/stencilCore.js"]
    subgraph APP["browser/"]
      IDX["js/index.js"]
      COREJS["js/core/"]
      UI["js/ui/"]
      API["js/console/"]
    end
    FB["JS fallback"]
    EXT["browser-extension/"]
    SRV["server/"]

    CORE -->|"wasm"| WASM
    WASM --> COREJS
    IDX --> COREJS
    IDX --> UI
    IDX --> API
    COREJS -.->|"no wasm"| FB
    EXT -->|"images"| IDX
    COREJS -.->|"REST + WS"| SRV
```

## Layers

`config/` + `utils.js` → `core/` (**no DOM**) → `eventBus/` (`core/emitter.js`) → `net/` → `llm/` →
console facade (`console/stencilApi.js`) → `ui/` → render. A layer imports only from its
left; `tests/layerBoundary.test.js` enforces it.

## Where things go

| Path | Holds | Rule |
|---|---|---|
| `index.html` | the single `<script type="module">` entry and the CSS link order | the link order **is** the cascade; the CSP meta is identical to the `nginx.conf` header |
| `css/` | `theme.css`, then `layout/`, `components/` (+ `chat/`), `animations/` | one file per section; tokens live in `theme.css` and are mirrored in `js/config/themeTokens.json` |
| `js/config/` | constants, hotkey + help-text registries, and every cross-surface table (`themeTokens`, `mediaTypes`, `uiStrings`, `events`, `motion`, `svgArt`, `logoStage`, `llm/`, `script/`) | **the canonical home of shared data**; the other surfaces embed or drift-test it |
| `js/config/script/fixtures/` | `cases.txt`, the `.stc` corpus: every case as a section of source, canonical dump and expected diagnostics | plain text, never JSON — `core/` has no JSON parser and reads this same file; a case named `err-*` must produce an error |
| `js/utils.js` + `js/utils/` | DOM, geometry, color, hotkey helpers | one import point; pure |
| `js/core/` | `DrawingApp` and its collaborators: renderer, storage, history, zoom/pan, coord table, formulas, projects store, `deepLink`, `projectFile`, `extensionBridge`, `stencilCore` (the wasm singleton) | **no DOM access** — it runs under `node --test` |
| `js/core/script*.js` | the `.stc` engine: lex → parse → templates → lower, plus `scriptDump` and the `scriptHandles` marshalling; `script.js` is the entry that binds wasm to the fallback | one file per `core/script/*.cpp`, op-for-op with it; pure — it resolves ops but calls no facade, and `vscode-extension/src/parser/` is a byte-equal copy of it |
| `js/eventBus/` | `appBus.js`, the app-wide event channel | channel names come from `config/events.json` |
| `js/net/` | abortable fetch, the connection store + manager, remote sync | every fetch goes through the one guard here |
| `js/llm/` | provider client, op-plan parser/executor, chat controller, the one shared chat session | validates every plan against `config/llm/opRegistry.json` before anything runs |
| `js/console/` | the `window.stencil` facade, one module per concern | frozen; every mutation routes through the same core methods the toolbar uses |
| `js/ui/` | string-returning components composed by `layout()`, one folder per region — `shell/` the frame, `canvas/` the stage and its pointers, `panel/` the side panels, `settings/`, plus `bindings/` wiring controls to the app and `motion/` + `dust/` | components return strings and emit on the bus — never reach into `net/` or `llm/` |
| `js/worker/` | the cross-tab projects sync worker | message constants shared with the app |
| `js/wasm/` | the generated `stencilCore.js` | gitignored; built by `npm run build-wasm` / CI |
| `sw.js`, `manifest.webmanifest`, `launch.html`, `vite.config.js` | the PWA shell, the `stencil://` bounce page, the optional single-file build | nothing in the app may depend on the build |
| `tools/` | the static server, the single-file build and its self-check | dev only |
| `tests/` | `node --test` suites, `wasm-parity.test.js`, `layerBoundary.test.js`, `dts.test.js` | never loads wasm — always the JS fallback |

## Entities

```mermaid
classDiagram
    class DrawingApp { +CodecLine[] lines
      +CropRect cropRect
      +string activeProjectId
      +RemoteLink remoteLink }
    class CodecLine { +XY[] points
      +string color
      +number thickness }
    class HistoryStack { +CodecLine[][] history
      +number historyStep }
    class LayoutPayload { +CodecLine[] lines
      +number imageWidth
      +WireCropRect cropRect }
    class ProjectFileDoc { +string format
      +number version
      +ProjectFileImage image }
    class ProjectMeta { +string id
      +number expiresAt
      +string address }
    class RemoteLink { +string address
      +string remoteId
      +number version }
    class ServerConnection { +string url
      +string token
      +ConnectionStatus status }
    class ConnectionManager { +ServerConnection[] connections
      +string[] expiredUrls }
    class Stencil { +apply(opts)
      +Project project
      +ChatFacade chat }
    class OpPlan { +PlanAction[] actions
      +PlanVariant[] variants
      +PlanAsk ask }
    class ChatController { +ChatMessage[] history
      +Attachment[] attachments
      +send(text) TurnResult }
    class ScriptBuffer { +string text
      +ScriptView[] views }
    DrawingApp "1" *-- "0..*" CodecLine : lines
    DrawingApp "1" *-- "1" HistoryStack : history
    HistoryStack "1" o-- "0..*" CodecLine : snapshots
    DrawingApp --> LayoutPayload : currentLayoutPayload()
    LayoutPayload "1" o-- "0..*" CodecLine : lines
    ProjectFileDoc "1" *-- "1" LayoutPayload : layout
    ProjectMeta --> RemoteLink : address, remoteId, remoteVersion
    DrawingApp "1" --> "0..1" RemoteLink : remoteLink
    DrawingApp "1" o-- "0..1" ConnectionManager : connections
    ConnectionManager "1" *-- "0..*" ServerConnection : connections
    Stencil --> DrawingApp : wraps
    ChatController --> Stencil : executeOpPlan
    ChatController --> OpPlan : one per turn
```

| Entity | What it is | Owned by / lifetime | Relates to |
|---|---|---|---|
| `DrawingApp` | The editor: image, provenance, lines, viewport, selection, settings and the project session as plain fields (`core/editorState.js`) | One per window, created by `js/index.js` | Mediates every collaborator; facade and toolbar call its methods |
| `CodecLine` | One drawn line, every field explicit (`core/linesCodec.js`, twin of `core/models.hpp`) | `DrawingApp.lines`; copied into snapshots and layouts | `HistoryStack`, `LayoutPayload` |
| `HistoryStack` | Line-snapshot undo/redo with a cursor and `MAX_STEPS`, shared with `core/state/HistoryStack.hpp` | One per app, reset on each project switch | `CodecLine` snapshots |
| `LayoutPayload` | The export subset of `LAYOUT_FIELDS` (`config/layoutFields.json`) plus `lines`; `cropRect` crosses as `{x,y,w,h}` | Built by `buildLayoutPayload` (`core/layout.js`); the browser definition is canonical | `ProjectFileDoc.layout`, the server's project `layout` |
| `ProjectFileDoc` | The `.stencil` document (`core/projectFile.js`); `format` is the sentinel, `version` the schema | Built by `buildProjectFile`, hardened by `parseProjectFile`; the browser definition is canonical | `LayoutPayload` |
| `ProjectMeta` | One registry row: name, colour, keywords, thumbnail, expiry, optional server link (`core/projectsStore.js`) | The `localStorage` registry behind `ProjectsStore`; expiry arithmetic twinned with `core/state/ProjectsStore.cpp` | `RemoteLink` |
| `RemoteLink` | The editor's link to a server project; `version` is the save-back guard (`core/remoteSyncController.js`) | `DrawingApp.remoteLink` for a server-linked session | `ProjectMeta`, `ServerConnection` |
| `ServerConnection` | One connected server: session token, REST surface, `/ws` feed (`net/serverConnection.js`); its `RemoteProjectRecord` is the server's `protocol.Project` | Created by `ConnectionManager.connect`, closed on disconnect | `ConnectionManager`, `RemoteLink` |
| `ConnectionManager` | The servers one session is connected to plus the expired-credential set; `snapshot()` is what `net/connectionStore.js` persists | Created lazily by the facade, one per app | `ServerConnection` |
| `Stencil` | The frozen `window.stencil` facade (`console/stencilApi.js`): settings, `Line` / `Point` / `Project` handles, `chat`, `llm` | `createStencil(app)` once at boot | Wraps `DrawingApp`; the executor's only target |
| `OpPlan` | A validated model reply: `reply`, `actions`, `variants`, `ask`, `warnings`, `chatOnly` (`llm/opPlan.js`); the op set is `config/llm/opRegistry.json`, canonical for every surface | Returned by `parseOpPlan` for one turn | `PlanAction`, `PlanVariant`, `PlanAsk` |
| `ScriptBuffer` | The one `.stc` the page is editing (`ui/scriptBuffer.js`): the text plus its views, so the script window and the context-menu flyout are two views of it and cannot diverge | Module state for the session; never persisted, so a reload starts empty | `wireScriptEditor` (`ui/scriptEditor.js`) |
| `ChatController` | The client-side conversation: replayed history, queued `Attachment`s, the send loop (`llm/chatController.js`); its transcript is the `ChatRow` log in `llm/chatSession.js` | One memoized per app via `sharedChatController` | `OpPlan`, `Stencil`, `LlmClient` |

## Patterns

| Pattern | Where | Notes |
|---|---|---|
| Facade over core | `createStencil(app)` in `console/stencilApi.js`, installed as `window.stencil` | Frozen; toolbar, hotkey, console script and op plan reach the same `DrawingApp` methods through it |
| Mediator | `DrawingApp` (`core/drawingApp.js`) | `HistoryStack`, `Renderer`, `Storage`, `RemoteSyncController`, `ProjectTransferController`, `InputController`, `ZoomPan` each take the app and reach back through it; they do not know one another |
| Command | `HistoryStack.push` / `undo` / `redo` behind `DrawingApp.saveHistory()` | Every undoable edit is a line snapshot, applied and reverted by one code path; wasm twin via `coreHandles.js` |
| Strategy | The provider `wire` in `llm/llmClient.js`, keyed by `config/llm/providers.json`; `FilterMode` in `core.applyFilterRGBA` | Selected by table lookup |
| Observer | `Emitter` (`core/emitter.js`) behind `TabsCoordinator` channels and `ServerConnection.onEvent`; `publish` / `subscribe` in `eventBus/appBus.js` over the `config/events.json` window events | The `stencil:*` window events are the contract the extension's content scripts read |
| Repository | `ProjectsStore` over a `StorageBackend`; `projectsBackend.js` moves payload keys to IndexedDB; `connectionStore.js` for saved servers; `chatStore.js` for chat documents | Callers see a synchronous `localStorage`-shaped contract |
| Chain of Responsibility | Every `ServerConnection` request: `normalizeUrl` → `isInsecureRemote` → `timeoutSignal()` → `isAuthStatus` / `isExpiredSession` (`net/urlRules.js`, `net/abortable.js`) | The one browser fetch guard; a refused credential lands in the `expired` status, not `error` |
| Adapter | `llm/adapters/{dialog,editor,media,project}.js` (the `ChatCapabilities` bag), `core/extensionBridge.js`, `core/deepLink.js` | Each translates an outside request into the same app methods the toolbar uses |
| State machine | `HoldDrawController` (`core/holdDraw.js`): idle → armed → drawing → idle, or armed → aborted | The host injects time and coordinates; wasm twin via `coreHandles.js` |
| Interpreter | `FormulaEngine` (`core/formulaEngine.js`), a recursive-descent evaluator over one variable | Port of `core/parse/formulaParser.cpp`; never `eval` |
| Interpreter + runner | `core/script*.js` lowers a `.stc` to an op stream; `console/scriptRunner.js` executes it | Port of `core/script/`; the parser never touches the editor and the runner never re-parses, so the language has one implementation and the browser only adds a target. Deliberately outside `llm/`: an op plan's caps guard model output, not the user's own script |

## Design

- **Boot.** `js/index.js` awaits `core.init()` on the singleton in `js/core/stencilCore.js`,
  which instantiates the wasm module and installs typed wrappers (long strings are
  marshalled over the heap), alongside `initProjectsBackend()`. It then constructs
  `DrawingApp`, installs `createStencil(app)` as `window.stencil`, wires chat persistence and
  `publishReady(app)` hands every `<stencil-*>` element the app.
- **An edit.** A pointer event reaches `InputController` / `PointerController`, which mutate
  `DrawingApp.lines` through the same methods the facade exposes. `saveHistory()` pushes a
  snapshot onto `HistoryStack`, `Renderer` repaints, `Storage.saveSoon()` debounces a
  `ProjectsStore.upsert` of `{ image, layout }`, and a server-linked session schedules
  `RemoteSyncController.scheduleRemoteSync()`.
- **Save and sync.** `ProjectsStore.upsert` writes the payload key first, so a quota failure
  leaves the registry untouched. `RemoteSyncController.saveToServer()` calls
  `saveRemoteProject(conn, link, { layout })` guarded by `RemoteLink.version`; a 409 merges
  the peer's lines with `mergeLines` and retries. A `project-event` on the `/ws` feed passes
  `shouldReloadFromEvent` before `reloadRemoteActive()` replaces the editor.
- **An LLM turn.** `sharedChatController(app)` memoizes one `ChatController`;
  `send(text)` replays history (`replayMessages`), calls `LlmClient.chat`, then `parseOpPlan`
  validates the reply against `SCHEMA` (built from `opRegistry.json` for the browser profile)
  and `executeOpPlan(plan, stencil, capabilities)` maps each op 1:1 onto a facade call.
  Variants and ask previews branch through `planSandbox` capture and restore;
  `runLoggedChatTurn` writes the `ChatRow`s the panel and context-menu chat both render.
- **A script.** `stencil.execScript(text)` (and the script window, and a dropped `.stc`) calls
  `runScript` in `js/console/scriptRunner.js`. It parses once through `js/core/script.js` —
  wasm when loaded, the fallback otherwise — and refuses to run anything at all when a
  diagnostic is an error. Each lowered op then maps onto one facade call, the same one the
  toolbar makes: `open`/`frame` → `stencil.load`, `crop` → `stencil.crop`, `filter` →
  `stencil.apply`, `line`/`rect` → `stencil.setLines` (combine), `layout` → an http(s)-only
  fetch then `stencil.applyLayout`, `undo`/`redo` → the history, `save` → `stencil.save`,
  renaming `stencil.current` first when the `@save` named a target.
  A source the browser cannot open — a local path, since there is no filesystem — is the one
  op that fails at run time rather than at parse time; a failure part-way leaves the edits
  already applied and names the line that stopped it.
- **Project files.** `js/core/projectFile.js` is the pure `.stencil` (de)serializer; IO is
  `ExportService`. It is an adapter-level format, not part of `core/`, so each surface
  serializes it independently and `e2e/` proves they agree on the same bytes. The document:

  ```jsonc
  { "format": "stencil-project", "version": 1, "name": "…",
    "color": "#rrggbb", "keywords": [], "source": "…", "resource": "…",   // each optional
    "blank": true, "blankColor": "#rrggbb",                                 // blank canvases only
    "image":  { "dataUrl": "data:image/png;base64,…", "ext": "png", "w": 1280, "h": 720 },
    "layout": { /* exactly buildLayoutPayload(): imageWidth, imageHeight, lines[], cropRect,
                  rotationQuarters, imageFilter, filterColor, pageSize, customPage*, formulas */ },
    "theme":  { "mode": "dark|light", "accent": "…" } }                     // optional, opt-in
  ```

  The console surfaces (cli, pystencil) may add an optional `chat` key (llm-contract §12.1,
  text only); the browser keeps chats in IndexedDB and on the server's `chat` file instead.
- **Deep links.** `js/core/deepLink.js` normalizes the inbound `#stencil=` fragment
  (`server` / `dataUrl` / `src` / `layout`) and builds the outbound `stencil://` and Telegram
  `?start=` links. A `.stc` and the incognito flag ride the same fragment for a hand-off that
  carries no picture: `index.js` runs the script once the launch settles, and leaves it in the
  script buffer rather than opening the window on someone else's script.
  `js/core/extensionBridge.js` answers the extension's state/import/switch requests through
  the same core methods.
- **A logo show.** Holding the header mark, typing a show's name, or calling
  `stencil.EasterEggs.<show>()` all reach `activateShow` (`ui/logoStageTrigger.js`), which asks
  `logoStageAllowed()` for a bare window — no stage up, not fullscreen, no open shell — and then
  either opens the stage or, for the pink show, makes the edit: a page if there is none, the tint
  through `settings`, and the heart as one `installLayout` step. `config/logoStage.json` is the
  table both front-ends resolve a show from: which accent opens which effect, the motion mode a
  styled effect also needs, and the custom hexes. The stage (`ui/logoStage.js`) is one canvas over
  the whole window painting the mark, its light (`logoStagePaint.js`, the `logoHover.css`
  keyframes at stage scale) and its cloud (`logoStageCloud.js`, over the shared grain kit); while
  it is up a capture-phase listener swallows the keyboard except Escape, so the editor is inert
  until it closes. `motionReduced()` keeps the stage and drops every loop.
- **Single-file build.** `vite.config.js` carries its rules inline (no plugins);
  `tools/assertSelfContained.js` re-reads the output, and `tests/singleFileBuild.test.js`
  fails `npm test` if a loader outruns `tools/singleFilePatterns.js`.

## Rules

1. **Every mutation goes through the facade.** Toolbar, hotkey, console script, LLM plan —
   all reach the same `DrawingApp` methods via `window.stencil`; the facade is typed in
   `stencilApi.d.ts`.
2. **wasm with a fallback.** Each module that calls the core keeps its JS body as the
   fallback and the two match op-for-op (`tests/wasm-parity.test.js`). No `eval` /
   `new Function` anywhere.
3. **`js/config/` is canonical.** A value another surface needs is a table here, never a
   literal in code.
4. **Ported modules stay byte-identical.** `ui/controlTooltip`, `numericInput`,
   `dropdownMenu`, `tipContent`, `scrollbarHover`, `dustCloud`, `motionIcons`,
   `motion/rectTween` and `llm/llmClient` are copied into `browser-extension/src/lib/` and
   pinned byte-identical
   (`browser-extension/tests/portParity.test.js`); `core/script*.js` is copied into
   `vscode-extension/src/parser/` and pinned the same way, in both directions
   (`vscode-extension/tests/parserParity.test.js`). Edit here, then re-copy.
5. **Typed boundary.** Every public module has a sibling `.d.ts`.
6. **Motion is decoration.** Every particle cloud is one canvas (`dustCloud.js`); never a
   DOM node per grain. The OS `prefers-reduced-motion` wins over every setting.
7. **Storage split.** Image-heavy project payloads live in IndexedDB; the small registry
   (names, thumbnails, expiry) in `localStorage`. Chat persistence is opt-in, text only,
   never in incognito.
8. **Nothing local goes outward.** The `#stencil=` fragment never reaches a server
   (`e2e/tests/browser/fragment-privacy`); deep links carry `{url, id, version}`, never a
   token.
9. **Every control is named, once.** An icon-only control's accessible name is its tooltip
   heading and a field's is the label beside it; `ui/ariaLabels.js` points the a11y tree at
   both, for the markup on the page and for whatever a modal renders later. Only a control
   whose name is nowhere on the page — a row's select box — writes its own `aria-label`.

## Tests

Every suite in `tests/` runs offline under `node --test`, on the JS fallback path: Node never
loads wasm. The DOM, `fetch`, `localStorage` and speech are stubbed once in `tests/helpers/`,
which `net/` and `llm/` take as injected implementations. The `wasm-parity*` suites are the
exception: they load the generated `js/wasm/stencilCore.js` and drive the JS reference and
the compiled core through one script, covering the pure ops and marshalling, the snapshot
codec and cursor, the expiry arithmetic and the stateful handle classes; they self-skip
without the artifact, which CI builds fresh.

The structural lints assert the design rather than behaviour: `layerBoundary` the import
direction and the `window.stencil` name, `sizeBudget` the line cap, the comment share and the
folder fan-out (a module and its `.d.ts` count once), `dts` every `.d.ts` against its module's exports in
both directions, and `events`, `themeTokens`, `csp` and `opRegistryCanon` the drift between
a config table and its consumer. `cssInventory` pins every declaration `index.html` loads,
file-blind, and `ui-markup` the static body ids of `layout()`. The fixture walkers run the
shared corpora through the real modules and are the reference the other surfaces' walkers
copy. Cross-surface reads live in `tests/helpers/`; the byte-identical port checks for the
copied modules live with their copies (`browser-extension/tests/portParity.test.js`,
`vscode-extension/tests/parserParity.test.js`), and `singleFileBuild` holds without
vite by checking the rewrite patterns still match.
