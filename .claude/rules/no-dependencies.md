---
description: This repo adds no dependencies, bundlers, or build steps — use the built-in tooling
---

# No new dependencies or build steps

Every subproject deliberately runs on its platform's built-in tooling. Do **not** reach for
the usual reflexes — adding a package, a bundler, a test framework, or a codec is almost
always the wrong move here. Match what each subproject already does:

- **browser/** — vanilla ES modules, **no build step to run the app, no bundler in its
  serving path**. Don't add npm runtime deps or a framework. Tests run on **Node's
  built-in runner** (`node --test`), not Jest/Vitest/Mocha. The one sanctioned exception,
  agreed with the user: `vite` as a **local dev dependency** powering the optional
  `npm run build` single-file bundle (`browser/vite.config.js` → `stencil.html`). Its
  bundling rules are written out inline in that config — **don't add vite plugins** — no
  lockfile is tracked (`browser/package-lock.json`, installed with `npm ci`), and nothing in
  the app may come to depend on the build.
- **extension/** — same: plain MV3, `node --test`, no deps.
- **core/** — **STL-only, codec-free, GUI-free** C++17. No Qt, no image codec, no DOM, no
  third-party libs. The one exception is Doctest — a single pinned header fetched at
  configure time (not a package). Codecs/HTTP/JSON belong in the adapters (Zig CLI, GUIs).
- **cli/** — Zig; only the `stb` image headers already vendored. **pystencil/** — **stdlib
  only**, driven over ctypes; no PyPI packages.
- **server/** (Go), **mcp/** (Rust), **bot/** (.NET) — keep dependency additions minimal and
  justified; prefer the standard library and what's already in the manifest.
- **e2e/** — the one harness with real dev dependencies (`@playwright/test`, `ws`), because
  it drives the shipped artifacts from outside. Nothing it installs may leak into a surface.

Every LLM client speaks HTTP with its platform's built-in facility. Adding an HTTP or JSON
library to reach a provider is never the answer.

If a task genuinely seems to need a new dependency or a build step, stop and confirm with the
user first — it contradicts the project's design and usually has an in-repo alternative.
