# Changelog — Stencil browser app

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. The editor as a vanilla ES-module web app with no build step.

### Added

- Runs the core as WebAssembly, with a behaviour-identical JavaScript fallback.
- Drawing by click or hold-to-draw; per-line colour, thickness, point size, style, name and
  visibility; rectangles and areas that lock and fill; a points table with numeric editing.
- Filters, crop, flip, rotate, zoom, pan and fit; a before/after compare; a blank-page
  creator; drag-and-drop and clipboard paste for images, videos and layout JSON; video
  frames extracted in the browser.
- Projects that autosave in the browser with an expiry, copy ("Make a copy"), and save to
  or load from a `.stencil` file, with live-sync to the linked file where the browser allows
  it.
- Windows for Projects, Servers, Links, the Assistant and its settings, Shortcuts, Visuals
  and Help; a canvas context menu; draggable toolbar icons; fullscreen; every shortcut
  rebindable.
- Light and dark themes with accent colours, a theme lens, interface animations (Dust,
  Water, Fire, Sliding, None) with a speed setting, and notices in the app or through the
  browser's own notifications.
- A script window for `.stc` with highlighting and in-place diagnostics, also as a flyout of
  the canvas menu and by dropping a `.stc` on the page.
- A console window taking the CLI's slash commands over the open project, and a CLI mode
  that turns the whole page into the terminal.
- A dockable or floating assistant chat with image and video attachments and variant cards,
  and opt-in saving of chats with projects.
- The address bar names the open project (`?project=<name>`): a paste, an open or a rename
  rewrites it and a close drops it, so a reload reopens the project; incognito leaves it alone.
- Copy and share menus, project and launch links, and "Open in…" the desktop app or the
  Telegram bot.
- The chainable `window.stencil` console API, typed in `js/console/stencilApi.d.ts`.
- A phone layout, an installable offline PWA, an optional single-file `stencil.html` build,
  a Docker image and the GitHub Pages deployment.
