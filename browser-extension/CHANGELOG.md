# Changelog — Stencil Chrome extension

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A Manifest V3 extension that finds a page's pictures and hands them
to the browser editor.

### Added

- Scans every way a page shows an image: `<img>` and its alternates, inline SVG, videos and
  posters, icons, preload hints, social meta images and every CSS image reference.
- A popup, a dockable side panel and a DevTools panel over the same list, with search and
  filters by format, size and kind, lazy rows, hover previews and on-page highlighting.
- Open in the editor (an in-page modal or a tab, normal or incognito), download, open in a
  new tab, and "Open in…" the desktop app or the Telegram bot.
- Quick crop: a page-aspect rectangle on any ISO format or a custom size, kept re-editable
  or cut into a new image.
- Drag a row onto the page, or page media onto the Stencil mark, for drop targets.
- A Stencil submenu in the image right-click menu, and "Copy Stencil link".
- Pins per site, browsed across sites in Options, and server projects as shared pins.
- An assistant that can focus, open or attach the pictures of the scanned page.
- Editor mode on an editor tab: the open editors with live previews, and importing a
  picture from another tab into the editor in front of you.
- Two opt-in scripting APIs: `stencil.extension` on the editor page and `window.stencil` on
  any page.
- Options for the editor URL, open mode, page size, appearance, share-link URL, servers and
  the assistant.
