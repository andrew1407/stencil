# Changelog — Stencil desktop app

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. The same editor as a native C++17 + Qt 6 app that links the core.

### Added

- The browser app's drawing, styling, filters, crop, flip, rotate, zoom, compare, page
  formats and keyboard shortcuts, with a status bar of pixel and page coordinates.
- Dialogs for Visuals & Settings, Projects, Controls & Shortcuts Info and the Keyboard
  Shortcuts editor; a system or in-window menu bar; autosave restored on the next launch.
- Themes, per-project accent colours, the interface animations and their speed, and notices
  in the app or through the OS notification centre.
- A console window with the CLI's slash commands, Tab completion and history, and CLI mode.
- A `.stc` script editor with Run, Open, Save and Copy, and scripts run by dropping a file.
- `.stencil` project files, opened by double-click, drag or file argument once installed.
- A dockable or floating assistant chat with image and video attachments; frames come from
  Qt Multimedia.
- Server connections over REST with invite links, server projects in the Projects dialog and
  version-conflict notices.
- Launch flags (`--src`, `--frame`, `--layout`, `--project`, `--incognito`, `--theme`),
  launch links, the `stencil://` scheme, file associations, drag-and-drop and app-icon
  menus; "Open In…" the browser app or the Telegram bot.
- Packages for macOS (`.dmg`, arm64 and x86_64), Linux (`.tar.xz`, arm64 and x86_64) and
  Windows (`.zip`, x86_64), each smoke-started before it is kept.
