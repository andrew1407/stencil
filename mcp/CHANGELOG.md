# Changelog — Stencil MCP server

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A Rust Model Context Protocol server over stdio that wraps the CLI.

### Added

- `stencil_edit`: the full pipeline in one call, with an optional thumbnail preview.
- `stencil_probe`: an image's size, format and alpha, or a video's length.
- `source_site`: scrape a page's media into a directory.
- `stencil_prompt`: a model-planned edit, validated by the CLI before it runs.
- `stencil_script`, `stencil_script_check`, `stencil_script_plan` and `stencil_script_emit`.
- `stencil_projects`, `stencil_project_update` and `stencil_project_file` for a
  collaboration server's projects.
- Delivery surfaces per call or by default: the output file, the desktop app, a browser
  launch URL.
- Read-only resources (the op registry, the `.stc` contract, page formats, colours, filters)
  and two prompts (`write_stc_script`, `print_page_layout`).
- Structured results with output schemas, tool annotations, progress notifications and
  cancellation.
- Confinement: writes land only inside the client's roots, only operator-allowed servers are
  reachable, tokens never appear as tool parameters, and CLI runs are timed and capped.
- A small Alpine Docker image with the static CLI and a minimal static FFmpeg pre-wired.
