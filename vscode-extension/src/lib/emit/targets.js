// What `--script-emit` can write, as the pick the user makes. The CLI reads the target off
// the output's extension, so the extension IS the choice; these are cli/CONTRACT.md §4.4's
// four suffixes, in the order a Stencil user wants them.
'use strict';

const EMIT_TARGETS = Object.freeze([
  { label: '.pystc', description: 'python — pystencil, with the Stencil icon' },
  { label: '.py', description: 'python — a plain module' },
  { label: '.stcjs', description: 'javascript — the browser facade, with the Stencil icon' },
  { label: '.js', description: 'javascript — a plain module' },
]);

const pickEmitTarget = async (vscode) => {
  const picked = await vscode.window.showQuickPick(EMIT_TARGETS, {
    title: 'Stencil: emit this script as', placeHolder: 'the extension picks the target',
  });
  return picked ? picked.label : null;
};

// Beside the script, under its own stem: shots.stc -> shots.pystc.
const emitTarget = (path, extension) => `${String(path).replace(/\.stc$/i, '')}${extension}`;

module.exports = { EMIT_TARGETS, emitTarget, pickEmitTarget };
