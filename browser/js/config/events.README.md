# events.json — the `stencil:*` DOM event channels

Every `stencil:*` event the app dispatches, in one place, keyed camelCase (the channel name
without the prefix, de-hyphenated). The strings are a wire contract: a dispatcher and its
listeners usually sit in different modules, and two channels cross the browser/extension
boundary entirely. A typo in a literal is silent — nothing throws, the listener simply never
fires — so no surface retypes one.

- **browser** — `import EVENTS from '../config/events.json' with { type: 'json' }`, then
  `EVENTS.ready`. Modules that re-export their own channel under a local name
  (`MOTION_EVENT`, `VOICE_STATE_EVENT`, `CHAT_POPUP_EVENT`, `CHAT_ATTACHMENTS_EVENT`,
  `VOICE_SETTINGS_EVENT`) take the value from here.
- **extension** — ships self-contained (MV3) and cannot import across subprojects, so its
  content scripts keep the literals; `browser-extension/tests/dataParity.test.js` pins them against
  this file, and `browser/tests/core/events.test.js` fails on a stray literal in browser code.

`stencil:ready` is dispatched on `document` (components wire off it once, `ui/base.js`);
every other channel is dispatched on `window`.

## Cross-surface channels

- `switchToSource` — the extension's `content/editorBridge.js` **dispatches** it into the
  editor page ("resume in the open editor tab"); `core/drawingApp.js` listens. Detail:
  `{ source, name }`.
- `registryChanged` — the editor **dispatches** it when the projects registry changes
  (`TabsCoordinator.projectsChanged`); the extension's `editorBridge` listens and
  republishes the registry to the background page.
