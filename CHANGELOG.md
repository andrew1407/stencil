# Changelog

Every release of Stencil, newest first.

Stencil carries **two kinds of version**. The **Stencil version** names a release of the
repository as a whole — the number a `v*` tag carries. Each **app** also has a version of
its own, declared in its own manifest: an app that changes moves its own number, and one
that did not keeps the number it had, so the two can differ from one release to the next.
Every release below opens with the table of what each app was at, and each app keeps a
changelog of its own beside its README — what that app added, changed and fixed.

## [1.0.0]

The first release: one shared C++ logic core, the editors built on it, and the adapters
around them.

| App (its changelog) | Path | Version |
|---|---|---|
| [Core](core/CHANGELOG.md) | `core/` | 1.0.0 |
| [Browser](browser/CHANGELOG.md) | `browser/` | 1.0.0 |
| [Desktop](desktop/CHANGELOG.md) | `desktop/` | 1.0.0 |
| [CLI](cli/CHANGELOG.md) | `cli/` | 1.0.0 |
| [pystencil](pystencil/CHANGELOG.md) | `pystencil/` | 1.0.0 |
| [Chrome extension](browser-extension/CHANGELOG.md) | `browser-extension/` | 1.0.0 |
| [VS Code extension](vscode-extension/CHANGELOG.md) | `vscode-extension/` | 1.0.0 |
| [MCP server](mcp/CHANGELOG.md) | `mcp/` | 1.0.0 |
| [Collaboration server](server/CHANGELOG.md) | `server/` | 1.0.0 |
| [Telegram bot](bot/CHANGELOG.md) | `bot/` | 1.0.0 |

### Across every app

- **One editing model.** Polylines, rectangles and filled areas over a picture, a video
  frame or a blank page; crop, left-right flip, quarter-turn rotation; the B&W, sepia,
  invert, contour and duotone-tint filters; undo and redo. The same edit gives the same
  pixels on every surface, because every surface runs the same core.
- **Page coordinates.** Pixel ↔ page conversion over the full ISO A/B/C series or a custom
  size, lengths in `px`, `cm`, `mm`, `in` and `%` (a number may be a fraction, `3/4in`), and
  optional `f(x,y)` formula transforms.
- **Layout JSON and `.stencil` project files.** A layout — lines, filter, crop, rotation,
  page — is one JSON document; a `.stencil` file bundles it with the original image and the
  project's metadata, and opens on any surface.
- **`.stc` scripts.** A plain-text language of `@` directives (`@source`, `@crop`, `@filter`,
  `@line`, `@rect`, `@layout`, `@frame`, `@use`, `@save`, `@undo` / `@redo`, `@stencil`
  templates) that batches the same edits over one image, a folder or a glob. A script with
  any error runs nothing. Specified in [`contracts/stc/`](contracts/stc/stc-contract.md).
- **AI assistant.** Describe an edit in words; the model answers with an op-plan that is
  validated against a whitelisted op set and run through the same operations as the toolbar,
  optionally as several variants. Providers: a local Ollama, any OpenAI-compatible server,
  Anthropic directly with your own session-only key, or a collaboration server's proxy.
  Specified in [`contracts/llm/`](contracts/llm/llm-contract.md).
- **Launch links.** One `?key=value` grammar — the browser app's URL, a `stencil://` link,
  the CLI's input — says what to open and how: a source and frame, a layout, a blank page, a
  project, a server to connect, the look, the assistant. Credentials are honoured only after
  `#`. Specified in [`contracts/launch/`](contracts/launch/launch-uri.md).
- **Collaboration.** Projects stored on a shared server, saved under a version guard, with
  every change announced on a live feed; invite links carry a server and its token.
- **Hand-offs.** A session moves between the browser app, the desktop app, the CLI console
  and the Telegram bot by link.
- **Guarded fetching.** Every surface that fetches a URL does it through one guard, and the
  address classes a guard refuses — link-local and cloud-metadata among them — are one
  shared table.

### Apps

What each app shipped is in its own changelog:

- **[Core](core/CHANGELOG.md)** — the C++17, STL-only, GUI-free and codec-free library every editor runs.
- **[Browser](browser/CHANGELOG.md)** — the editor as a vanilla ES-module web app with no build step.
- **[Desktop](desktop/CHANGELOG.md)** — the same editor as a native C++17 + Qt 6 app that links the core.
- **[CLI](cli/CHANGELOG.md)** — a Zig command-line tool that recompiles the core and drives it headlessly.
- **[pystencil](pystencil/CHANGELOG.md)** — a stdlib-only Python package that drives the core over `ctypes`.
- **[Chrome extension](browser-extension/CHANGELOG.md)** — a Manifest V3 extension that finds a page's pictures and hands them
  to the browser editor.
- **[VS Code extension](vscode-extension/CHANGELOG.md)** — editor support for `.stc`, `.stcjs` and `.pystc`.
- **[MCP server](mcp/CHANGELOG.md)** — a Rust Model Context Protocol server over stdio that wraps the CLI.
- **[Collaboration server](server/CHANGELOG.md)** — a Go server that stores and shares projects.
- **[Telegram bot](bot/CHANGELOG.md)** — a .NET bot that drives the CLI and the collaboration server from a chat.

[1.0.0]: https://github.com/andrew1407/stencil/releases/tag/v1.0.0
