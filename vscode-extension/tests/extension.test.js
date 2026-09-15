// activate() is wiring: every feature registers, every disposable lands on the context.
import test from 'node:test';
import assert from 'node:assert/strict';

import { installVscodeStub, makeContext, makeVscode } from './helpers/vscodeStub.js';

const boot = () => {
  const { vscode, calls } = makeVscode();
  const host = installVscodeStub(vscode);
  const extension = host.require('extension.js');
  const ids = host.require('lib/ids.js');
  const context = makeContext();
  extension.activate(context);
  return { calls, context, extension, host, ids };
};

test('activate registers diagnostics, semantic tokens and every contributed command', () => {
  const { calls, context, ids, host } = boot();
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
test('the script and the facade each get a completion and a hover provider', () => {
  const { calls, ids, host } = boot();
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
test('activation opens no browser, no terminal and no output channel', () => {
  const { calls, host } = boot();
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
test('the listeners are the ones the two live features need, and no others', () => {
  const { calls, host } = boot();
  try {
    assert.deepEqual(Object.keys(calls.events).sort(),
      ['change', 'close', 'config', 'open', 'save', 'theme', 'visibleEditors']);
  } finally {
    host.restore();
  }
});

test('deactivate is a no-op; the context owns the disposables', () => {
  const { extension, host } = boot();
  try {
    assert.equal(extension.deactivate(), undefined);
  } finally {
    host.restore();
  }
});

test('extension.js is wiring only — it holds no logic of its own', () => {
  const { host } = boot();
  try {
    const exported = Object.keys(host.require('extension.js')).sort();
    assert.deepEqual(exported, ['activate', 'deactivate']);
  } finally {
    host.restore();
  }
});
