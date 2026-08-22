# `.stencil` project-file conformance vectors

Pin the portable single-file project format shared by browser (`js/core/projectFile.js`,
the reference), desktop (`io/fileStore.cpp`), cli (`project.zig`), pystencil
(`editor.py`) and bot (`Domain/Project/StencilProjectFile.cs`).

Vector shape: `{ name, file, expect: "ok"|"error", project?, errorIncludes?, divergences? }`
- `file` — the literal `.stencil` document text handed to the parser (may be malformed
  on purpose). All documents stay far below the 32 MiB `MAX_PROJECT_FILE_CHARS` gate.
- `expect: "ok"` — `parseProjectFile(file).project`, after a JSON round-trip (forced
  layout keys with undefined values drop out), must deep-equal `project` EXACTLY: the
  full normalized shape `{ name, color, keywords, source, resource, blank, blankColor,
  image, layout, theme }`. Key order is not compared.
- `expect: "error"` — the parse must fail; `errorIncludes` is a substring of the
  browser's error message (other surfaces map these to their own error kinds — match
  on the CASE, not the text).
- `divergences` — optional informational `{ "<surface>": "one-line summary" }` inventory
  of measured surface differences on the vector; never asserted — walkers ignore it, and
  each surface pins its own divergent expectation in its local override file.

Image dataUrls are tiny valid-prefix stubs (`data:image/png;base64,AAAA`) — the format
parser never decodes pixels, so conformance needs no real image bytes.
