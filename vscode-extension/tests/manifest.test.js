// package.json is the contract with the extension host: an id that agrees with src/lib/ids.js
// on one side and with VS Code on the other. Nothing here runs the editor; it reads the
// manifest the way the host does.
import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
const ids = require('../src/lib/ids.js');
const contributes = manifest.contributes;
const here = (rel) => fileURLToPath(new URL(rel, import.meta.url));

test('the extension is CommonJS, and its entry point exists', () => {
  assert.equal(manifest.type, undefined, 'a root "type" would break require() in src/');
  assert.equal(manifest.main, './src/extension.js');
  assert.ok(existsSync(here(`../${manifest.main}`)));
  assert.equal(manifest.name, 'stencil-stc');
  assert.equal(manifest.engines.vscode, '^1.90.0');
});

test('the language is contributed under the id the code uses', () => {
  const [language] = contributes.languages;
  assert.equal(language.id, ids.LANGUAGE_ID);
  assert.deepEqual(language.extensions, [ids.FILE_EXTENSION]);
  assert.deepEqual(manifest.activationEvents, [`onLanguage:${ids.LANGUAGE_ID}`]);
  assert.equal(contributes.grammars[0].scopeName, ids.SCOPE_NAME);
});

test('every contributed path exists on disk', () => {
  const paths = [
    contributes.languages[0].configuration,
    contributes.grammars[0].path,
  ];
  for (const path of paths) assert.ok(existsSync(here(`../${path}`)), `${path} is missing`);
});

test('the manifest declares exactly the three commands the code registers', () => {
  const declared = contributes.commands.map((c) => c.command).sort();
  assert.deepEqual(declared, Object.values(ids.COMMANDS).sort());
  for (const command of contributes.commands) {
    assert.match(command.title, /^Stencil: /, `${command.command} is not namespaced in the palette`);
  }
});

test('the run button and the keybinding point at the run command, scoped to .stc', () => {
  const [item] = contributes.menus['editor/title'];
  assert.equal(item.command, ids.COMMANDS.runScript);
  assert.equal(item.when, `resourceLangId == ${ids.LANGUAGE_ID}`);
  const [binding] = contributes.keybindings;
  assert.equal(binding.command, ids.COMMANDS.runScript);
  assert.equal(binding.key, 'ctrl+alt+r');
  assert.equal(binding.mac, 'cmd+alt+r');
  assert.match(binding.when, new RegExp(`editorLangId == ${ids.LANGUAGE_ID}`));
});

test('the settings are the two the code reads, under the stencil section', () => {
  const properties = contributes.configuration.properties;
  assert.deepEqual(Object.keys(properties).sort(),
    Object.values(ids.SETTINGS).map((k) => `${ids.CONFIG_SECTION}.${k}`).sort());
  assert.equal(properties['stencil.cliPath'].type, 'string');
  assert.equal(properties['stencil.cliPath'].default, '', 'the CLI is found, not assumed');
  assert.equal(properties['stencil.checkOnType'].type, 'boolean');
});

test('@vscode/vsce is the only dependency, dev-only and exactly pinned', () => {
  assert.equal(manifest.dependencies, undefined, 'nothing ships at runtime');
  assert.deepEqual(Object.keys(manifest.devDependencies), ['@vscode/vsce']);
  assert.match(manifest.devDependencies['@vscode/vsce'], /^\d+\.\d+\.\d+$/, 'an exact pin');
});

test('the scripts are the two the CI job runs, and the lockfile is tracked', () => {
  assert.equal(manifest.scripts.test, 'node --test');
  assert.match(manifest.scripts.package, /^vsce package --out /);
  assert.ok(existsSync(here('../package-lock.json')), 'npm ci needs a tracked lockfile');
  assert.ok(existsSync(here('../.vscodeignore')), 'the .vsix must not carry tests or node_modules');
});

test('every module in src/ has a sibling .d.ts naming its exports', () => {
  const walk = (dir) => readdirSync(dir, { withFileTypes: true }).flatMap((entry) => (
    entry.isDirectory() ? walk(join(dir, entry.name)) : [join(dir, entry.name)]));
  const modules = walk(here('../src')).filter((f) => f.endsWith('.js'));
  assert.ok(modules.length >= 17, `only ${modules.length} modules were seen`);
  for (const module of modules) {
    const declaration = `${module.slice(0, -3)}.d.ts`;
    assert.ok(existsSync(declaration), `${module} has no sibling .d.ts`);
    const named = readFileSync(declaration, 'utf8');
    assert.match(named, /export (?:declare |const |type |interface )/, `${declaration} declares nothing`);
  }
});

test('.vscodeignore keeps the tests out and the parser copies in', () => {
  const ignore = readFileSync(here('../.vscodeignore'), 'utf8');
  for (const line of ['tests/**', 'node_modules/**']) {
    assert.ok(ignore.includes(line), `${line} must be excluded from the .vsix`);
  }
  assert.ok(!/^src\//m.test(ignore), 'src/ ships whole — the parser copies run in the editor');
});
