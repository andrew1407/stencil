# Changelog — Stencil for VS Code

The version here is the app's own; the release of Stencil as a whole it shipped in, and
every other app's version at that release, are in the [repository changelog](https://github.com/andrew1407/stencil/blob/main/CHANGELOG.md).

## [1.0.0]

Shipped in Stencil 1.0.0, the first release. Editor support for `.stc`, `.stcjs` and `.pystc`.

### Added

- Highlighting from the real parser, so a word is coloured by the statement it sits in, with
  per-family colours in `stencil.colors`.
- Completion of what is legal at the caret, hovers with grammar and an example, and
  diagnostics with their codes while typing and again on save from the CLI.
- Run script, Run script on an image, Check script and Emit script as…, through the CLI.
- `.stcjs` and `// @use stencil` files completed and hovered from the `window.stencil`
  facade; `.pystc` and `# @use stencil` files run on Python.
- Open in Stencil Web and in Stencil Desktop, each also incognito; Run in Stencil Web
  Console through the built-in JavaScript debugger, for a file, a selection or an
  expression.
- Add facade typings to a workspace (`stencil.d.ts`).
- File icons for scripts, their JavaScript and Python flavours, and `.stencil` projects.
- Packaged as a local `.vsix`.
