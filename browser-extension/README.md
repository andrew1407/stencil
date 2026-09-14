# Stencil Image Picker — Chrome extension

A Manifest V3 Chrome/Edge extension that lists, searches and filters every image on the
current page and lets you download it, open it in a tab, or send it to the
[Stencil browser editor](../browser/) — as an in-page modal by default — including a quick
page-aspect crop. Vanilla JS, no build step. How it is put together:
[`ARCHITECTURE.md`](ARCHITECTURE.md).

## Install (unpacked)

1. Serve the editor: from [`../browser/`](../browser/) run `npm run serve`
   (default `http://localhost:8080/`).
2. `chrome://extensions` → enable **Developer mode** → **Load unpacked** → select
   this `browser-extension/` folder.
3. (Optional) **Options**: editor URL, open mode (modal / tab), default page size,
   appearance, server connections, the AI assistant, and the two scripting-API toggles.

## Using it

**Toolbar popup** (click the extension icon) scans the active tab for every way an image can
appear — `<img>` and `srcset`/`<picture>` alternates, `<input type=image>`, inline SVG images,
`<video>` frames and posters, favicons and `apple-touch-icon`s, preload hints, `og:image` /
`twitter:image` meta, web-app-manifest icons, and every CSS image reference
(`background-image`, `content`, `border-image-source`, `list-style-image`, `mask-image`,
`cursor`, `shape-outside`, `image-set()`). Search by name or URL, filter by format, by
min/max size, and by kind (`<img>` / background). Rows render lazily as you scroll; hover a
thumbnail for an enlarged preview. The header's moon/sun button flips light/dark for every
extension surface (**Options → Appearance** has the full System / Light / Dark choice).

- **Click** a thumbnail or name → open it in the editor (a prompt picks incognito vs
  normal). **Double-click** → quick crop.
- The row's **⋯** menu: **Download**, **Open in new tab**, **Open in editor**, **Editor
  (incognito)**, **Crop…**, and **Open in…** (desktop app via a `stencil://` link; the
  Telegram bot for shared server rows once a bot username is configured). A video row also
  offers its **poster** separately.
- **📌** pins an image for this site; with a server connected it can also store the image as
  a server project. **Options → Pinned images** browses every pin across sites.
- The **⇥** header button docks the same view as a **side panel**, which stays open while
  you work and re-scans when you switch tabs or a page loads. A **Stencil** tab in
  **DevTools** shows the same view pinned to the inspected tab.
- **Drag a row out** onto the page for a four-quadrant drop overlay (open here · incognito ·
  new tab · crop), or drag page media onto the header's Stencil mark to get the same choices
  mid-drag. Both need a surface that stays open while you drag: the side panel or DevTools.

**Image right-click menu** — a **Stencil** submenu next to the browser's own image actions:
*Open in editor*, *Open in Stencil (incognito)*, *Crop image in Stencil…*, and *Open in
desktop app* when a desktop URL scheme is configured. It also works on CSS backgrounds, images
under a click-catcher overlay, and links straight to an image file.

**How things open.** *Open in editor* opens the full editor in a new tab. *Resume in open
editor* (on an already-opened row) focuses the editor tab you already have and switches it to
that image's project. *Crop* opens a small in-page modal (falling back to its own tab on pages
whose CSP blocks frames); only *Open in editor* from there spawns the full tab.

**Quick crop** mirrors the editor's crop model — a page-aspect rectangle in original-image
pixels (drag to move, corner resize, scroll to zoom) on any ISO A/B/C format or a custom cm
size — then **Keep original** (full image + crop applied, re-editable) or **Cut cropped part**
(bake the region into a new image), optionally incognito.

**Server connections** — **Options → Server connections** connects to one or more
[collaboration servers](../server/README.md) (URL + optional token; an invite link
`<url>#token=<tok>` pasted as the URL supplies its own token). Each server's stored projects
appear as **shared pins** after the page's own images, with a golden outline and a server
badge; the popup refreshes them on a light poll while open.

## AI assistant

An **Assistant** section sits at the bottom of the popup, side panel and DevTools panel
(collapsed by default; the **✦** header button expands and focuses it). It chats over the
surface's live scan: the model can **focus** an image on the page, **open** it in the editor
(with optional crop/rotate/filter/layout/page), or **attach** it for vision analysis. Drop or
paste images and videos onto the section to attach them; the send button becomes **Stop**
mid-turn, 🗑 clears the conversation. Nothing is persisted — a popup's chat ends when it
closes.

Providers (Ollama, any OpenAI-compatible server, or a collaboration server's Anthropic
proxy) are set in **Options → AI assistant**; with the provider `none` the section and the
✦ button are hidden. The extension reaches providers through its host permissions, so no
CORS setup is needed. Setup guide: the [root README](../README.md#ai-assistant--setting-up-a-model);
the op set is the extension profile in
[`contracts/llm/llm-profiles.md`](../contracts/llm/llm-profiles.md).

## Editor mode

When the active tab **is** the configured Stencil editor, the popup and side panel switch to
two other sections:

- **Open editors** — every open editor tab, with a live canvas preview, project and image
  name, `this tab` / `incognito` badges, and a search box. Click a row to focus that tab; its
  ⋯ menu adds **Switch project ▸** and **Open in new tab**.
- **Images from another page** — pick any other open tab and the familiar scan runs on it.
  Clicking a row, dropping it, or *Open in editor* **imports the image into the editor tab
  you're standing in** — no new tab. If the editor already holds an image a chooser asks:
  **new project**, **replace image**, or **replace, keeping annotations**.

The Assistant section works the same way here, over the picked page's scan; its `open` op
imports into your editor tab. The DevTools panel stays the classic surface.

### `stencil.extension` — the same capabilities from the editor's console

On the configured editor origin the extension adds a `stencil.extension` slot to the
editor's own `window.stencil` facade (gated by **Options → Editor page scripting API**,
default **on**; `null` when the extension isn't installed or the toggle is off). Every method
with an answer is async and rejects with a real `Error` when the call can't be served;
`openInNewTab` and `crop` are one-way and return the facade.

```js
await stencil.extension.editors();                  // every open editor tab
await stencil.extension.current;                    // …this tab's state alone
await stencil.extension.focus(tabId);               // raise that tab + its window
await stencil.extension.switchProject('p3');        // this tab → another of its projects
const tabs = await stencil.extension.tabs();        // other open pages worth scanning
const imgs = await stencil.extension.images(tabs[0].tabId);   // scan one of them
await stencil.extension.open(0, { mode: 'new' });   // import imgs[0] into THIS editor tab
stencil.extension.openInNewTab(imgs[2]);            // the ordinary #stencil= hand-off
stencil.extension.crop('https://example.com/pic.png');   // the quick-crop tool
```

| member | arguments | resolves to |
|---|---|---|
| `editors({ thumbnails = true } = {})` | `thumbnails: false` skips the canvas capture | `[{ tabId, windowId, url, title, active, current, ready, projectId, projectName, hasImage, imageName, imageSize, incognito, thumbnail, projects }]` — one row per open editor tab; `current` marks the calling tab, `ready:false` a tab whose bridge stayed silent |
| `current` | — (a getter) | `{ projectId, projectName, hasImage, imageName, imageSize, incognito, thumbnail, projects }` for **this** tab |
| `focus(tabId)` | `tabId: number` | `{ tabId, windowId }`; the tab is activated and its window raised |
| `switchProject(projectId, { tabId } = {})` | project id; `tabId` defaults to this tab | `{ projectId, projectName }`; an unknown id rejects with `unknown project` |
| `tabs({ currentWindowOnly = false } = {})` | — | `[{ tabId, title, url, host, label }]` — the open http(s) pages that can be scanned |
| `images(tabId, { limit } = {})` | `tabId: number`; `limit` caps the scan (default 1000) | the scan entries for that tab — `{ kind, src, videoUrl, posterUrl, w, h, poster, meta, tabId, resource, name }` plus their own `open()` / `crop()`; remembered, so `open()`'s index form indexes into it |
| `open(target, { tabId, mode, page, crop, incognito, resource } = {})` | `target` = a scan entry, an index into the last `images()` result, or a URL string | imports into an **open** editor tab → `{ tabId, mode, projectId, projectName }`. `mode` is `'new'`, `'replace'`, `'replace-keep'` or `'ask'` (the default: imports straight into a blank editor, otherwise rejects with `editor already holds an image`, carrying `err.needsChoice` + `err.state`) |
| `openInNewTab(target, { incognito, resource } = {})` | as `open` | nothing — the `#stencil=` hand-off in a **new** tab; returns the facade |
| `crop(target, { album } = {})` | as `open` | nothing — the quick-crop path; returns the facade |

A string `target` is always a URL, never a CSS selector.

## Page scripting API (`window.stencil`, opt-in)

Off by default. Enable **Options → Page scripting API** to inject a `window.stencil` object
into every page's main world, mirroring the popup/context-menu actions for scripting from the
DevTools console. Entries hold the live DOM element.

```js
// Lists (these honor the live filters below):
stencil.items;                     // every image reference on the page (all of the below)
stencil.images;                    // kind 'image': <img> (+ srcset/<picture> alternates),
                                   //   <input type=image>, <svg><image>/<feImage>, favicons,
                                   //   preload/prefetch hints, og:image/twitter:image <meta>
stencil.backgrounds;               // kind 'background': every CSS image reference
stencil.icons;                     // icon/metadata images (e.meta) — excluded from `images`
stencil.videos;                    // just the <video> elements
stencil.posters;                   // the poster image of every <video> that has one
stencil.pins;                      // just the entries currently pinned on this site

// Live filters (mirror — and stay in two-way sync with — the popup's controls):
stencil.formats;                   // a per-format toggle map: { png: true, jpg: true, mp4: true }
stencil.formats.png = false;       // …turn a format off
stencil.kinds;                     // a per-category toggle map: { image, background, video, poster, meta }
stencil.kinds.video = false;       // …hide a whole category
stencil.searchText = 'logo';       // name/URL substring filter
stencil.minWidth = 200;            // size bounds: minWidth/maxWidth/minHeight/maxHeight (or null)
stencil.highlightOnPage = true;    // outline the (filtered) images on the page
stencil.resetFilters();            // clear all filters + the highlight → the facade

// One-off queries (ignore the live filters above):
stencil.search('logo');            // entries whose name or URL contains "logo"
stencil.format('png');             // entries of a given format ('png' or '.png')
stencil.size({ minW: 200, minH: 200 });  // entries within pixel bounds (unknown sizes pass)

const e = stencil.items[0];
e.element; e.kind;                 // live DOM node; 'image' | 'background' | 'video'
e.url; e.name; e.format;           // the URL, a derived "file.ext" name, 'png' | 'jpg' | …
e.width; e.height;                 // intrinsic px where known (0 if not)
e.poster; e.meta;                  // true for a poster entry / an icon-metadata entry
e.pinned;                          // pinned on this site? — assignable get/set
e.isEdited;                        // opened in an editor before? (read-only)
e.open();                          // → open in the editor (in-page modal); returns the facade
e.open({ newTab: true, incognito: true });   // open opts: newTab, incognito, poster, frame
e.crop({ album: true });           // → quick-crop tool; crop opts: album, poster
e.pin(); e.unpin();                // same as e.pinned = true/false

// Or act on a raw element / URL directly (throws if it isn't a loadable image):
stencil.open(document.querySelector('video'), { poster: true });
stencil.open('https://example.com/pic.png', { newTab: true });   // a string is a URL, NOT a selector
stencil.crop('https://example.com/pic.png', { album: true });

// Pin / unpin a target — an entry, an items index, an element, a URL, or an array:
stencil.pin(0);  stencil.pin(document.images[2]);  stencil.pin(['https://example.com/a.png', 3]);
stencil.unpin(0);

// Inspect a target before acting on it — never throws:
stencil.grabbable(el);             // → boolean: can Stencil grab this?
stencil.detect(el);                // → { kind, url, name, format, element, hasFrame, hasPoster,
                                   //     pinned, isEdited, listed } — or null

stencil.enabled = false;           // turn the whole feature back off (get/set)
```

The filters and the highlight stay in two-way sync with the popup, so `stencil.formats.png =
false` here unticks the popup's chip and vice-versa. Pinning here writes the shared pin
store. Like the editor's `window.stencil`, the object is hard-guarded and non-enumerable. It
is **not** the editor-page API: `stencil.extension` (above) is a separate object on a separate
toggle, injected only on the editor origin.

## Tests

```bash
# from this directory (browser-extension/)
npm test        # or: node --test
```

Node's built-in runner, no dependencies. Besides the behaviour suites, `portParity.test.js`
and `dataParity.test.js` pin the modules and `src/config/` tables copied from `browser/`
byte-for-byte, and `manifestSecurity.test.js` pins the CSP and `web_accessible_resources`.
