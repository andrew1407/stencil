---
description: This repo adds no dependencies, bundlers, or build steps — use the built-in tooling
---

# No new dependencies or build steps

Every subproject deliberately runs on its platform's built-in tooling. Do **not** reach for
the usual reflexes — adding a package, a bundler, a test framework, or a codec is almost
always the wrong move here. Match what each subproject already does:

- **browser/** — vanilla ES modules, **no build step, no bundler**. Don't add npm runtime
  deps, Webpack/Vite/Rollup, or a framework. Tests run on **Node's built-in runner**
  (`node --test`), not Jest/Vitest/Mocha.
- **extension/** — same: plain MV3, `node --test`, no deps.
- **core/** — **STL-only, codec-free, GUI-free** C++17. No Qt, no image codec, no DOM, no
  third-party libs. The one exception is Doctest — a single pinned header fetched at
  configure time (not a package). Codecs/HTTP/JSON belong in the adapters (Zig CLI, GUIs).
- **cli/** — Zig; only the `stb` image headers already vendored. **pystencil/** — **stdlib
  only**, driven over ctypes; no PyPI packages.
- **server/** (Go), **mcp/** (Rust), **bot/** (.NET) — keep dependency additions minimal and
  justified; prefer the standard library and what's already in the manifest.

If a task genuinely seems to need a new dependency or a build step, stop and confirm with the
user first — it contradicts the project's design and usually has an in-repo alternative.
