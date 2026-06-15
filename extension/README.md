# Stencil Image Picker — Chrome extension

A Manifest V3 Chrome/Edge extension that lists, searches and filters every image
on the current page and lets you download it, open it in a tab, or send it to the
[Stencil browser editor](../browser/) — as an **in-page modal** by default —
including a quick page-aspect crop.

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
- **Click** a thumbnail or name → open it in the editor (a quick prompt picks
  incognito vs normal). **Double-click** → quick crop.
- A `⋯` button opens a floating menu **next to the icon** with: **Download**,
  **Open in new tab**, **Open in editor**, **Editor (incognito)**, **Crop…**.

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

> **Loading unpacked in current Chrome stable** is gated; if `Load unpacked` is
> unavailable, use [Chrome for Testing](https://googlechromelabs.github.io/chrome-for-testing/).

### Icons

Chrome's toolbar and context menu need **raster PNG** icons (an SVG renders as the
generic puzzle piece), so `icons/icon-{16,32,48,128}.png` are used by the manifest.
They're generated from the **shared** browser favicon — which is reused, not copied:
`icons/icon.svg` is a symlink to `../../browser/favicon.svg` (the in-page popup/crop
logos use it). Regenerate the PNGs after changing the favicon:

```bash
# from the repo root (macOS; uses Quick Look)
for s in 16 32 48 128; do qlmanage -t -s $s -o /tmp/ql browser/favicon.svg && \
  mv /tmp/ql/favicon.svg.png extension/icons/icon-$s.png; done
```

> The symlink keeps a single source of truth for unpacked development; if you zip
> the folder for distribution, replace it with a real copy of `favicon.svg`.

## Project structure

```
manifest.json            MV3 manifest
package.json             `npm test` → node --test
icons/                   icon.svg → symlink to ../../browser/favicon.svg; icon-*.png generated from it
src/
  background/background.js  service worker: image context menu + tab-fallback relay
  popup/    popup.html|css|js   image list, search/filters, floating actions, preview
  crop/     crop.html|css|js    quick page-aspect crop (zoom, custom size)
  options/  options.html|js     editor URL, open mode, default page size
  lib/
    stencil.js       settings, fetch→dataURL, launch-URL builder, launchEditor
    overlay.js       in-page editor modal (also injected into pages)
    cropGeometry.js  port of the editor's crop math (kept behaviour-identical)
    imageScan.js     the page scanner (injected via chrome.scripting)
    filters.js       format / search / size filtering (pure)
    theme.css        shared dark-theme palette (linked by popup/crop/options)
tests/                   node:test unit tests for the pure modules
```

## Tests

Pure logic (crop geometry, filtering, launch-URL/filename helpers) runs under
Node's built-in runner — no dependencies:

```bash
# from this directory (extension/)
npm test        # or: node --test
```
