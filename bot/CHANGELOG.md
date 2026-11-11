# Changelog — Stencil Telegram bot

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A .NET bot that drives the CLI and the collaboration server from a chat.

### Added

- Sources: a photo, an image file, an image link, a video frame, a blank page, a layout
  JSON, a `.stencil` project or a `.stc` script, with caption commands and albums.
- Edit commands, each with an inline button: crop, rotate, flip, filter, reset, undo, and
  downloads of the result, the layout JSON and the project.
- Drawing: lines, rectangles and polygons, with a pen of colour, thickness, point size,
  style and fill.
- `/script` for a one-line or uploaded `.stc`.
- The assistant: `/prompt`, a hands-free `/chat` mode, per-user chat-API profiles, Stop and
  Retry, variants as an album, and opt-in chat persistence in the server project.
- Page scraping into the chat (`/sourcesite`) or as the working image (`/sourceupload`).
- Server projects: connect (also by invite link), list, fetch, create, save, expiry, and
  live `/sync` with a peer's saves.
- Deep links in both directions: `/start` payloads from "Open in… → Telegram", and `/link`
  to the desktop app.
- A per-user allowlist that fails closed, bounded update queues, and session state in
  memory or Redis.
- A Docker image with the CLI and `ffmpeg`.
