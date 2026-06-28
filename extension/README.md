# Stencil Image Picker — Chrome extension

A Manifest V3 Chrome/Edge extension that lists, searches and filters every image
on the current page and lets you download it, open it in a tab, or send it to the
[Stencil browser editor](../browser/) — as an **in-page modal** by default —
including a quick page-aspect crop. Vanilla JS, no build step.

## Features

**Toolbar popup** (click the extension icon)
- Scans the active tab for `<img>`, inline `<svg><image>` and CSS
  `background-image` URLs (deduped, capped at 1000).
- **Search** by file name or URL, a **format dropdown** (All + the common web
  formats: png/jpg/gif/webp/svg/avif/bmp/ico/tiff, plus any others the page uses),
  **min/max width & height** (empty = no bound), and toggles to **include `<img>`**
  and/or **background-image** sources. Background images have no size in the DOM, so
  they're shown by default and measured lazily so the size filters can apply once known.
- **Lazy rendering**: rows are added in batches as you scroll (good for image-heavy pages).
- Hover a thumbnail for an enlarged preview.

**Server connections & shared pins** (`lib/connections.js`)
- Connect to one or more [Stencil collaboration servers](../server/README.md) from
  **Options → Server connections** (URL + optional token). `addServer()` validates/issues a
  token via `POST /auth/token` and persists the connection in `chrome.storage.local`, so it
  survives popup reopen and is readable by the side panel / DevTools panel; `removeServer()`
  drops it. Each connection is listed there with a remove button.
- Each connected server's stored projects (those with an image) become **shared pins** in
  the popup list: `loadConnections() → collectSharedPins()` pulls them over REST and they
  render **after** the page's own images with a **golden outline + server badge** (the
  `server` icon glyph) to set them apart from local (gray) pins. Their bytes are fetched over
  the connection's **Bearer-authed** download endpoint (`fetchProjectImage` →
  `GET /projects/{id}/files/{kind}`), since a bare `<img src>` can't send the token. The
  **thumbnail** pulls the edited **`result`** variant (the project's saved filter + lines
  baked in — what the editor exported), falling back to the **`original`** when no result was
  ever saved; the **editor/crop hand-off** always pulls the **`original`** so the editor can
  re-apply the saved filter/lines (the `result` has them baked in and couldn't be re-edited).
  Clicking a shared row opens the (original) server image in the editor; the `⋯` menu offers
  open/incognito/here/crop on it.
- **Real-time refresh:** the popup re-pulls shared pins on a light **poll while open**
  (`SHARED_POLL_MS`, ~8 s) — MV3 popups are short-lived, so this is simpler and more robust
  than holding a background `/ws` events socket open. It also reacts to `chrome.storage`
  connection changes immediately (add/remove a server in Options → the popup updates without
  a rescan).
- **Pin target picker:** pinning an image (the 📌 button) always makes the local pin, and —
  when a server is connected — offers to **also store it on a server** via `createProject`
  (`POST /projects` with the image's `source`/`resource`): a **checkbox** for a single
  connection, a **picker** for several (`pinTargetMode` / `connectionByUrl`). The new project
  shows up as a shared pin on the next poll.
- **Click** a thumbnail or name → open it in the editor (a quick prompt picks
  incognito vs normal). **Double-click** → quick crop.
- A `⋯` button opens a floating menu **next to the icon** with: **Download**,
  **Open in new tab**, **Open in editor**, **Editor (incognito)**, **Crop…**. For a
  **video** the menu also carries a **Video preview image** group (open in a tab,
  download, open/crop the poster in the editor) — the poster acts independently of
  the current frame, mirroring the page right-click menu's submenu.
- The `⇥` header button **docks the same view as a side panel** (Chrome side panel).

**Side panel** (the `⇥` button, or `chrome.sidePanel`) — the identical search /
filters / image-video list, but **persistent**: it stays open while you work the
page and **re-scans automatically** when you switch tabs or a page finishes loading.
Runs the same `popup.js` controller (`src/sidepanel/`).

**DevTools panel** — a **Stencil** tab inside Chrome DevTools (next to Elements,
Console, …), the same view again but **pinned to the inspected tab** rather than
the focused one, and **re-scans when that page navigates**. Registered by
`src/devtools/devtools.js`; the panel page (`src/devtools/panel.html`) reuses the
same `popup.js` controller, which targets `chrome.devtools.inspectedWindow.tabId`.

**Image right-click menu** — a **Stencil** submenu next to the browser's own
“Open image / Save image”: *Open in editor*, *Open in Stencil (incognito)*,
*Crop image in Stencil…*.

**How things open**
- **Open in editor** → the full editor in a **new browser tab** (so its own
  multi-project / cross-tab UI shows any editors you already have open).
- **Crop** → a small **in-page modal** (an iframe of the quick-crop tool) so you
  stay on the page; only when you press *Open in editor* there does it spawn the
  full tab. On pages whose CSP `frame-src`/`child-src` blocks the frame, the crop
  tool **falls back to its own tab** (the tool posts a `ready` handshake; if the
  modal host doesn't hear it in time it reopens the tool as a tab).

**Quick crop** — mirrors the editor's crop model (a page-aspect rectangle in
original-image pixels; drag inside to move, corner-only resize, **scroll/buttons to
zoom**). Page size **A3 / A4 / Custom** (custom takes width × height in cm). Then
choose **Keep original** (full image + crop applied, lossless/movable) or **Cut
cropped part** (bake the region into a new image). Both honour an **incognito**
checkbox; the chosen page size is carried into the editor.

## How the hand-off works

The extension fetches the image bytes (host permissions bypass page CORS → the
editor never sees a tainted canvas), converts them to a `data:` URL, and opens the
editor with the payload in the URL **fragment**:

```
http://localhost:8080/#stencil=<encodeURIComponent(JSON)>
```

`JSON = { dataUrl, name, crop?, page?: {size:'A3'|'A4'|'custom', width?, height?}, incognito? }`.
The fragment never reaches the server. The editor consumes it in
`DrawingApp.applyExternalLaunch()` (`browser/js/core/drawingApp.js`), applies the
page size, loads the image (with the crop), then strips the fragment.

## Install (unpacked)

1. Serve the editor: from [`../browser/`](../browser/) run `npm run serve`
   (default `http://localhost:8080/`).
2. `chrome://extensions` → enable **Developer mode** → **Load unpacked** → select
   this `extension/` folder.
3. (Optional) **Options**: editor URL, open mode (modal / tab), default page size.

## Project structure

```
manifest.json            MV3 manifest
package.json             `npm test` → node --test
src/
  background/background.js  service worker: image context menu + tab-fallback relay
  popup/    popup.html|css|js   image list, search/filters, floating actions, preview
  sidepanel/ sidepanel.html|css  docked side-panel surface (reuses popup.js + popup.css)
  devtools/ devtools.html|js, panel.html|css  DevTools "Stencil" panel (reuses popup.js)
  crop/     crop.html|css|js    quick page-aspect crop (zoom, custom size)
  options/  options.html|js     editor URL, page size, pinned-images viewer, server connections
  lib/
    stencil.js       settings, fetch→dataURL, launch-URL builder, launchEditor
    overlay.js       in-page editor modal (also injected into pages)
    cropGeometry.js  port of the editor's crop math (kept behaviour-identical)
    imageScan.js     the page scanner (injected via chrome.scripting)
    filters.js       format / search / size filtering (pure)
    pins.js          pinned-images store, keyed by (site, source URL) (pure + storage)
    connections.js   collaboration-server connections + SHARED pins (REST mirror of server/internal/protocol)
    messages.js      cross-context message `type`/`source` constants (no magic strings)
    theme.css        shared dark-theme palette (linked by popup/crop/options)
tests/                   node:test unit tests for the pure modules
```

## Page scripting API (`window.stencil`, opt-in)

Off by default. Enable **Options → Page scripting API** to inject a `window.stencil`
object into every page's main world, mirroring the popup/context-menu actions for
scripting from the DevTools console. Entries hold the **live DOM element**.

```js
// Lists (these honor the live filters below):
stencil.items;                     // every <img>/<svg image>/<video>/background on the page
stencil.images;                    // just <img> + inline <svg><image>
stencil.backgrounds;               // just CSS background-image elements
stencil.videos;                    // just the <video> elements
stencil.posters;                   // the poster image of every <video> that has one
stencil.pins;                      // just the entries currently pinned on this site

// ── Live filters (mirror — and stay in two-way sync with — the popup's controls) ──
stencil.formats;                   // a per-format toggle map: { png: true, jpg: true, mp4: true }
stencil.formats.png = false;       // …turn a format off (Object.keys lists those present)
stencil.kinds;                     // a per-category toggle map: { image, background, video, poster }
stencil.kinds.video = false;       // …hide a whole category (the popup's include checkboxes)
stencil.searchText = 'logo';       // name/URL substring filter
stencil.minWidth = 200;            // size bounds: minWidth/maxWidth/minHeight/maxHeight (or null)
stencil.maxHeight = 1000;
stencil.highlightOnPage = true;    // outline the (filtered) images on the page (alias: highlightOnImage)
stencil.resetFilters();            // clear all filters + the highlight → the facade

// One-off queries (ignore the live filters above):
stencil.search('logo');            // entries whose name or URL contains "logo"
stencil.format('png');             // entries of a given format ('png' or '.png')
stencil.size({ minW: 200, minH: 200 });  // entries within pixel bounds (unknown sizes pass)

const e = stencil.items[0];
e.element; e.kind;                 // live DOM node; 'image' | 'background' | 'video'
e.url;                             // the image/video/background URL
e.name;                            // a derived "file.ext" name
e.format;                          // 'png' | 'jpg' | 'webp' | … ('' if undetectable)
e.width; e.height;                 // intrinsic px where known (0 if not, e.g. unloaded bg)
e.poster;                          // true for a stencil.posters entry
e.pinned;                          // pinned on this site? — assignable get/set
e.isEdited;                        // was/is this image opened (edited) in an editor? (read-only)
e.open();                          // → open in the editor (in-page modal); returns the facade
e.open({ newTab: true, incognito: true });   // open opts: newTab, incognito, poster, frame
e.crop();                          // → quick-crop tool
e.crop({ album: true });           // crop opts: album, poster
e.pin(); e.unpin();                // pin/unpin this entry (chainable); same as e.pinned = true/false

// Or act on a raw element / URL directly (throws if it isn't a loadable image):
stencil.open(document.querySelector('video'), { poster: true });
stencil.open('https://example.com/pic.png', { newTab: true });   // a string is a URL, NOT a selector
stencil.crop('https://example.com/pic.png', { album: true });

// Pin / unpin a target — an entry, a stencil.items index, an element, a URL, or an array:
stencil.pin(0);                    // pin stencil.items[0]
stencil.pin(document.images[2]);   // pin an element
stencil.pin(document.querySelector('img.hero'));   // a querySelector result is just an element
stencil.pin(['https://example.com/a.png', 3]);   // mixed array, chainable
stencil.unpin(0);

// Inspect a target before acting on it (entry | items-index | element | URL) — never throws:
stencil.grabbable(el);             // → boolean: can Stencil grab this? (valid open/crop/pin target)
stencil.grabbable(document.querySelector('div.banner'));   // false if it has no image/video/bg source
stencil.detect(el);                // → { kind, url, name, format, element, hasFrame, hasPoster,
                                   //     pinned, isEdited, listed } — or null if nothing grabbable
stencil.detect(0).listed;          // does the target currently appear in stencil.items?
[a, b, c].every(stencil.grabbable);   // validate a batch before stencil.pin([...])

stencil.enabled = false;           // turn the whole feature back off (get/set)
```

The filters and the highlight **stay in two-way sync with the popup**: the filters share the
popup's persisted `chrome.storage.local` state (the MAIN-world API can't touch `chrome.*`,
so the ISOLATED `content/pageApiBridge.js` proxies storage for it), and the highlight shares
the popup's `<style id="stencil-hl-style">` element. So `stencil.formats.png = false` or
`stencil.kinds.video = false` here is reflected in the popup's checkboxes (and vice-versa),
and `stencil.highlightOnPage = true` ticks the popup's highlight box.

`pinned` and `isEdited` are backed the same way: the bridge pushes the current site's pinned
source URLs and the opened-images ledger into the page so the getters answer synchronously.
Pinning here writes the shared pin store, so it lights up the popup's row (gray outline, the
📌 button) and appears in **Options → Pinned images** — a cross-site browser of every pin,
filterable by the site it was pinned on, with open-in-new-tab and unpin. `isEdited` reflects
the **already-opened** ledger (an image opened in an editor); it's read-only here.

`open`/`crop`/`pin` accept a **DOM element** (so `document.querySelector('img')` works), a scanned
entry, a `stencil.items` index, or a **URL string** — a string is always a URL, never a CSS
selector (use `querySelector` yourself and pass the element). To check a target *before* acting,
`stencil.grabbable(target)` returns whether Stencil can grab it (it carries an image/video/background
source, or a capturable video frame), and `stencil.detect(target)` returns a descriptor of what it
sees (`kind`, `url`, `name`, `format`, `hasFrame`, `hasPoster`, `pinned`, `isEdited`, `listed`) or
`null` — both never throw, unlike the actions. `listed` says whether the target currently survives
the live filters (appears in `stencil.items`).

The **Options-page settings** are a different layer: `editorUrl`, default page size,
**Mark already-opened images**, **Sort opened first**, **Show pinned** are the extension's
cross-page preferences (also in `chrome.storage`, behind the service worker) and are *not*
part of this page API — there's no `stencil.markOpened`, because this surface is about the
images on the *current page*. Set those in the Options page (or the popup's own toggles).

Like the editor's `window.stencil`, this object is **hard-guarded**: every method, read-only
getter, and scanned entry rejects reassignment (`stencil.open = 0` / `e.url = 'x'` throw),
so only the documented setters (`enabled`, the filter controls) mutate anything. It's also
non-enumerable, so `console.log(stencil)` stays clean while access and autocomplete still work.

Architecture: a MAIN-world script (`content/pageApiMain.js`) defines the API and
scans the DOM; since the main world has no `chrome.*`, action requests are
postMessage'd to an ISOLATED bridge (`content/pageApiBridge.js`) that relays them to
the service worker, which reuses the same `openEditorTab` / `launchEditorModal` /
`launchCrop` hand-off as the popup. The pure scan helpers live in `lib/pageImages.js`
(unit-tested); the MAIN-world file mirrors them (it can't import modules).

## Tests

Pure logic (crop geometry, filtering, launch-URL/filename helpers) runs under
Node's built-in runner — no dependencies:

```bash
# from this directory (extension/)
npm test        # or: node --test
```
