# Changelog — Stencil core

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](../CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. The C++17, STL-only, GUI-free and codec-free library every editor runs.

### Added

- Geometry and hit-testing, chain edits, crop-window geometry and crop specs.
- Whole-image RGBA8 transforms over caller-owned buffers: rotate, flip, crop, the software
  line rasteriser, the per-pixel filters, contour detection and an area-averaged downscale.
- Colour parsing (hex and CSS names) and the luma formulas.
- The recursive-descent `f(x,y)` formula parser with its page and image constants — no
  `eval`, one depth cap.
- Pixel ↔ page conversion and the ISO A/B/C page table, with the locale's default unit.
- Snapshot undo/redo history, co-edit line merge, the projects store with expiry, zoom/pan
  state and the hold-to-draw state machine.
- The `.stc` language: lexer, parser, templates, lowering to op-plan actions, diagnostics
  with stable codes, and the canonical dump.
- A `JSON.parse`-equal reader under caps and a `JSON.stringify`-equal writer.
- The LLM op-plan validator over the shared op registry, resolved per surface.
- Two `extern "C"` ABIs: one compiled to WebAssembly for the browser, and `cliApi.h` for the
  CLI and pystencil. The library never throws.
