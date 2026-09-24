// The shared rig for the browser commands: a stub host with an instance configured and a
// debug session that answers the facade probe, plus readers for what each route did.
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { installVscodeStub, makeDocument, makeEditor, makeVscode } from './vscodeStub.js';

export const APP = 'http://localhost:8080/';

export const PNG = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==', 'base64');

export const withHost = async (options, body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-webcmd-'));
  const { vscode, calls } = makeVscode({
    ...options,
    settings: { 'stencil.webUrl': APP, ...(options.settings ?? {}) },
    // Every console run asks `typeof window.stencil` first, then evaluates.
    debugAnswers: options.debugAnswers ?? [{ result: "'object'" }, { result: 'Stencil' }],
  });
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, dir, vscode, web: await host.import('webCommands.js') });
  } finally {
    host.restore();
    rmSync(dir, { recursive: true, force: true });
  }
};

export const open = (vscode, { text = '', languageId = 'stencil-script', path = '/tmp/demo.stc' } = {}) => {
  const document = makeDocument({ text, languageId, path });
  vscode.window.activeTextEditor = makeEditor(document);
  return document;
};

export const openedPayload = (calls) => {
  const url = String(calls.opened.at(-1));
  return JSON.parse(decodeURIComponent(url.slice(url.indexOf('#stencil=') + 9)));
};

export const sent = (calls) => calls.sessions.at(-1).requests.map((r) => r.args.expression);
