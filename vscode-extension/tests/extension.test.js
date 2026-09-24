// activate() is wiring: every feature registers, every disposable lands on the context.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeContext, makeVscode } from './helpers/vscodeStub.js';

const boot = async () => {
  const { vscode, calls } = makeVscode();
  const host = installVscodeStub(vscode);
  const extension = await host.import('extension.js');
  const ids = await host.import('lib/ids.js');
  const context = makeContext();
  extension.activate(context);
  return { calls, context, extension, host, ids };
};

test('activate registers diagnostics, semantic tokens and every contributed command', async () => {
  const { calls, context, ids, host } = await boot();
  try {
    assert.equal(calls.collections.length, 1, 'one diagnostic collection');
    assert.equal(calls.semanticProviders.length, 1, 'one semantic tokens provider');
    assert.deepEqual([...calls.commands.keys()].sort(), Object.values(ids.COMMANDS).sort());
    assert.ok(context.subscriptions.length >= 8, 'every disposable is owned by the context');
  } finally {
    host.restore();
  }
});

// Two languages get suggestions and explanations: .stc from its own vocabulary, JavaScript
// from the facade's. Each provider names the languages it answers for.
test('the script and the facade each get a completion and a hover provider', async () => {
  const { calls, ids, host } = await boot();
  try {
    const selectors = (list) => list.map((p) => JSON.stringify(p.selector));
    assert.deepEqual(selectors(calls.completionProviders), [
      JSON.stringify({ language: ids.LANGUAGE_ID }),
      JSON.stringify([{ language: ids.JS_LANGUAGE_ID }, { language: 'javascript' }]),
    ]);
    assert.deepEqual(selectors(calls.hoverProviders), selectors(calls.completionProviders));
  } finally {
    host.restore();
  }
});

// Nothing is spawned, opened or launched by activation itself.
test('activation opens no browser, no terminal and no output channel', async () => {
  const { calls, host } = await boot();
  try {
    assert.deepEqual(calls.opened, []);
    assert.deepEqual(calls.terminals, []);
    assert.deepEqual(calls.channels, []);
    assert.deepEqual(calls.debugConfigs, []);
  } finally {
    host.restore();
  }
});

// open/save/close/change are diagnostics'; visibleEditors, config and theme are decorations'.
test('the listeners are the ones the two live features need, and no others', async () => {
  const { calls, host } = await boot();
  try {
    assert.deepEqual(Object.keys(calls.events).sort(),
      ['change', 'close', 'config', 'open', 'save', 'theme', 'visibleEditors']);
  } finally {
    host.restore();
  }
});

test('deactivate is a no-op; the context owns the disposables', async () => {
  const { extension, host } = await boot();
  try {
    assert.equal(extension.deactivate(), undefined);
  } finally {
    host.restore();
  }
});

test('extension.js is wiring only — it holds no logic of its own', async () => {
  const { host } = await boot();
  try {
    const exported = Object.keys(await host.import('extension.js')).sort();
    assert.deepEqual(exported, ['activate', 'deactivate']);
  } finally {
    host.restore();
  }
});
