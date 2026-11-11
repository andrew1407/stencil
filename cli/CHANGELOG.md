# Changelog — Stencil CLI

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A Zig command-line tool that recompiles the core and drives it headlessly.

### Added

- One-shot pipeline: an image, a video frame (through `ffmpeg`), a blank page or a
  `.stencil` project in; crop, `--album`, flip, rotate, layout, filter and `--thumbnail`;
  PNG, JPEG, BMP, TGA or a `.stencil` out.
- Local files and `http(s)` URLs as input, and launch links read as the flags they carry.
- `--script`, `--script-check`, `--script-plan` and `--script-emit`, which rewrites a `.stc`
  as JavaScript for the browser facade or Python for pystencil.
- `--source-site` scrape mode: a page's images, videos, posters and CSS backgrounds,
  filtered by category, format, name and size, paged and downloaded.
- `--prompt` for a model-planned edit, with `--plan-check` validating a plan for the
  adapters.
- A collaboration-server client: fetch a project by name, write a result back, publish a new
  project, list, inspect and update projects and download their stored files; tokens from
  flags, invite links or the environment.
- `--probe`, `--merge-lines`, `--no-clobber`, `--confine-output` and `--strict-fetch`.
- Console mode (`--console`): slash commands over one working image with full history, line
  editing, completion, clipboard paste and copy, multiple server connections, live `/sync`
  over the TCP feed, `/prompt` and `/llm`, and hand-offs to the browser and desktop apps
  with a live-linked `.stencil` file.
- A full-screen TUI (`--console-full-screen`) with a pinned logo, scrollback, mouse
  selection and accent themes.
- Release binaries for Linux (static musl), macOS and Windows, and a Docker image.
