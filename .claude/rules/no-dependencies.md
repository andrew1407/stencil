---
description: This repo adds no dependencies, bundlers, or build steps — use the built-in tooling
---

# No new dependencies or build steps

Every subproject deliberately runs on its platform's built-in tooling. Do **not** reach for
the usual reflexes — adding a package, a bundler, a test framework, or a codec is almost
always the wrong move here. Match what each subproject already does:

- **browser/** — vanilla ES modules: no build step to run the app, no bundler in its serving
  path, no npm runtime dependency or framework; tests on **`node --test`**, never
  Jest/Vitest/Mocha. Sanctioned exception one: **`vite`**, a local dev dependency for the
  optional `npm run build` single-file bundle (`browser/vite.config.js` → `stencil.html`), its
  rules inline in that config (**no vite plugins**), its lockfile tracked and installed with
  `npm ci`; nothing in the app may come to depend on the build.
- **browser-extension/** — plain MV3, `node --test`, no deps.
- **vscode-extension/** — plain ES modules against the `vscode` API the editor provides (never a
  dependency), `node --test` behind a hand-written stub, the parser a byte-equal copy, never a
  package. Sanctioned exception two: **`@vscode/vsce`**, exactly pinned, a dev dependency for
  `npm run package` only; its lockfile is tracked and the `.vsix` carries no `node_modules`.
- **core/** — **STL-only, codec-free, GUI-free** C++17 (`core-changes.md` §4); its one exception
  is Doctest, a single pinned header fetched at configure time.
- **cli/** — Zig, with only the vendored `stb` image headers. **pystencil/** — **stdlib only**
  over ctypes; no PyPI packages.
- **server/** (Go), **mcp/** (Rust), **bot/** (.NET) — additions minimal and justified; prefer
  the standard library and what the manifest already has.
- **e2e/** — the one harness with real dev dependencies (`@playwright/test`, `ws`), because it
  drives the shipped artifacts from outside. Nothing it installs may leak into a surface.

Every LLM client speaks HTTP with its platform's built-in facility. Adding an HTTP or JSON
library to reach a provider is never the answer.

If a task genuinely seems to need a new dependency or a build step, stop and confirm with the
user first — it contradicts the project's design and usually has an in-repo alternative.
