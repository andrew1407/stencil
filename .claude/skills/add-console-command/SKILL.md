---
name: add-console-command
description: >-
  The file-by-file procedure for adding a slash command to the Stencil CLI's interactive
  console (`stencil --console` / `--repl`) and its pystencil twin — verb, handler, rendering,
  help text, TUI goldens. Use when asked to add, rename or change a `/command` in the CLI
  console or the pystencil REPL.
---

# Add a console command (cli, and its pystencil twin)

Handlers return values; only `console/` and `app/` write to a terminal (`cli/ARCHITECTURE.md`).

1. `cli/src/console/commands/verbs.zig` — the `Verb` enum member and its word match in `verbOf`.
2. `cli/src/console/handlers/<area>/` — the handler (`handlers/` itself is at the folder cap:
   a new one joins its area's folder, and `handlers.zig` re-exports it). It returns values; it
   does not print.
3. `cli/src/console/ui.zig`, `cli/src/console/screen.zig`, `cli/src/console/render/` — the
   rendering. Terminal output lives only here.
4. Help text, then re-record the TUI goldens:
   `cd cli && STENCIL_UPDATE_PINS=1 zig build test`. Never re-record
   the effect goldens under `cli/tests/pins/fx/` for a command — they guard the easter eggs.
5. If the command is part of the shared console profile, mirror it in
   `pystencil/pystencil/cli/commands/`, declared through `pystencil/pystencil/cli/registry.py`.
   Update `cli/README.md`.
6. A command the GUI consoles take too is a row in `common/config/console/commands.json`
   (`surfaces` names them; `cliMode: true` keeps it to the secret CLI mode) with a handler in
   `browser/js/console/shell/handlers{Edit,Session}.js` and in
   `desktop/src/app/console/consoleCommands{Edit,Net}.cpp`; each surface's parity test fails
   until the row and the handler both exist.
