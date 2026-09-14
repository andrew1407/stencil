// The command that opens VS Code's own token-colour setting. What it must never do is
// overwrite a customization the user already has.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { installVscodeStub, makeContext, makeVscode } from './helpers/vscodeStub.js';

const withHost = (body, settings = {}) => {
  const { vscode, calls } = makeVscode({ settings });
  const host = installVscodeStub(vscode);
  try {
    return body({
      calls, colors: host.require('colors.js'), settings,
      legend: host.require('semanticTokens.js').TOKEN_TYPES,
    });
  } finally {
    host.restore();
  }
};

test('register contributes the fourth command id, and it is not a CLI one', () => {
  withHost(({ calls, colors }) => {
    const context = makeContext();
    colors.register(context);
    assert.deepEqual([...calls.commands.keys()], ['stencil.configureColors']);
    assert.equal(context.subscriptions.length, 1);
    assert.equal(calls.terminals.length, 0, 'nothing is spawned');
  });
});

test('an unset setting is seeded with every rule, then opened for editing', async () => {
  await withHost(async ({ calls, colors }) => {
    await colors.configureColors();
    const [update] = calls.updates;
    assert.equal(update.key, colors.SETTING);
    assert.deepEqual(update.value, { '[*]': { rules: { ...colors.DEFAULT_RULES } } });
    assert.equal(update.global, true, 'user settings, not the workspace');
    const [opened] = calls.executed;
    assert.equal(opened.id, 'workbench.action.openSettingsJson');
    assert.deepEqual(opened.args[0], { revealSetting: { key: colors.SETTING, edit: true } });
  });
});

test('a setting the user already wrote is opened, never overwritten', async () => {
  const mine = { '[Dark Modern]': { rules: { keyword: '#ff0000' } } };
  await withHost(async ({ calls, colors }) => {
    await colors.configureColors();
    assert.deepEqual(calls.updates, [], 'nothing was written');
    assert.equal(calls.executed[0].id, 'workbench.action.openSettingsJson');
  }, { 'editor.semanticTokenColorCustomizations': mine });
});

test('an empty object counts as unset, so the command is still useful', async () => {
  await withHost(async ({ calls, colors }) => {
    await colors.configureColors();
    assert.equal(calls.updates.length, 1);
  }, { 'editor.semanticTokenColorCustomizations': {} });
});

test('every rule names a type in the legend, and the README block agrees', () => {
  withHost(({ colors, legend }) => {
    for (const type of Object.keys(colors.DEFAULT_RULES)) {
      assert.ok(legend.includes(type), `${type} is not in the legend`);
    }
    const readme = readFileSync(new URL('../README.md', import.meta.url), 'utf8');
    const block = readme.match(/```jsonc\n([\s\S]*?)\n```/)[1];
    const documented = Object.fromEntries([...block.matchAll(/"(\w+)":\s*"(#[0-9a-f]{6})"/g)]
      .map(([, type, hex]) => [type, hex]));
    assert.deepEqual(documented, { ...colors.DEFAULT_RULES },
      'the README block drifted from colors.js DEFAULT_RULES');
  });
});
