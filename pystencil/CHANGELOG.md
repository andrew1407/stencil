# Changelog — pystencil

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. A stdlib-only Python package that drives the core over `ctypes`.

### Added

- The chainable `Editor` facade: load a path, URL, bytes or an `Image`; blank pages; rotate,
  flip, crop, filters, page formats, `draw` and `apply_layout`; undo, redo and reset; save
  an image, a layout or a `.stencil` project.
- `Image`, an RGBA8 buffer that decodes and encodes PNG, BMP and JPEG.
- `Layout`, `Line` and `Point` dataclasses that round-trip the browser's layout JSON.
- `Core` / `get_core()`, the low-level binding to the core's transforms and tables.
- `.stc` scripts: `Editor.script`, `run_script` and `parse_script`.
- `ServerConnection` and `ConnectionManager`: a REST client for the collaboration server,
  with change tracking by polling.
- `pystencil.llm`: `LlmClient`, `LlmConfig`, a multi-turn `Chat`, `Editor.prompt` and
  `execute_op_plan`, with opt-in chat persistence in the project.
- The `python -m pystencil` / `stencil-py` command line: the CLI's one-shot flags, scrape
  mode, the script flags and a `/command` REPL.
- The native library builds on demand from `core/` and rebuilds when a source is newer.
