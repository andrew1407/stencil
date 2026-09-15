// The rig the two JavaScript-side suites share: a stub host, the two flavours of buffer, and
// readers for a position and a hover's range.
import { installVscodeStub, makeDocument, makeVscode } from './vscodeStub.js';

export const withHost = async (settings, body, workspaceFolder = '') => {
  const { vscode, calls } = makeVscode({ settings, workspaceFolder });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, vscode, hints: host.require('jsHints.js') });
  } finally {
    host.restore();
  }
};

export const stcjs = (text) => makeDocument({ languageId: 'stencil-js', path: '/tmp/a.stcjs', text });
export const js = (text) => makeDocument({ languageId: 'javascript', path: '/tmp/a.js', text });
export const at = (line, character) => ({ line, character });
export const span = ({ range }) =>
  [range.start.line, range.start.character, range.end.line, range.end.character];
