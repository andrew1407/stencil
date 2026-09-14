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

test('activate registers diagnostics, semantic tokens and the three commands', () => {
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
