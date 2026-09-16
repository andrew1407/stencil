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
  assert.equal(manifest.displayName, 'Stencil', 'the name the Extensions view shows');
  assert.equal(manifest.engines.vscode, '^1.90.0');
});

test('the language is contributed under the id the code uses', () => {
  // .stc stays FIRST: this file and grammar.test.js both read index 0 as the script's own.
  const [language] = contributes.languages;
  assert.equal(language.id, ids.LANGUAGE_ID);
  assert.deepEqual(language.extensions, [ids.FILE_EXTENSION]);
  // A contributed language wakes the host by itself; `javascript` is the one that is not ours.
  assert.deepEqual(manifest.activationEvents, ['onLanguage:javascript']);
  assert.equal(contributes.grammars[0].scopeName, ids.SCOPE_NAME);
});

// .stcjs is JavaScript that drives window.stencil. The editor's own JS service owns the
// language; this tree adds an icon, the facade's words, and a grammar that defers.
test('the JavaScript flavour is a third language that defers to source.js', () => {
  const js = contributes.languages.find((l) => l.id === ids.JS_LANGUAGE_ID);
  assert.ok(js, `no ${ids.JS_LANGUAGE_ID} language`);
  assert.deepEqual(js.extensions, [ids.JS_FILE_EXTENSION]);
  const grammar = contributes.grammars.find((g) => g.language === ids.JS_LANGUAGE_ID);
  assert.equal(grammar.scopeName, ids.JS_SCOPE_NAME);
  const rules = JSON.parse(readFileSync(here(`../${grammar.path}`), 'utf8'));
  assert.equal(rules.scopeName, ids.JS_SCOPE_NAME);
  assert.deepEqual(rules.patterns, [{ include: 'source.js' }]);
  // A plain .js opts in per buffer, so the host must wake for javascript too.
  assert.ok(manifest.activationEvents.includes('onLanguage:javascript'));
});

// .stencil is contributed for its icon and to open as JSON, and its grammar must defer rather
// than re-spell the JSON rules. Contributing it already wakes the host, so it is not spelled out.
test('the project file is a second language that defers to source.json', () => {
  const project = contributes.languages.find((l) => l.id === ids.PROJECT_LANGUAGE_ID);
  assert.ok(project, `no ${ids.PROJECT_LANGUAGE_ID} language`);
  assert.deepEqual(project.extensions, [ids.PROJECT_FILE_EXTENSION]);
  assert.ok(!manifest.activationEvents.some((e) => e.includes(ids.PROJECT_LANGUAGE_ID)),
    'a contributed language activates the host on its own — spelling it out is redundant');
  const grammar = contributes.grammars.find((g) => g.language === ids.PROJECT_LANGUAGE_ID);
  assert.equal(grammar.scopeName, ids.PROJECT_SCOPE_NAME);
  const rules = JSON.parse(readFileSync(here(`../${grammar.path}`), 'utf8'));
  assert.equal(rules.scopeName, ids.PROJECT_SCOPE_NAME);
  assert.deepEqual(rules.patterns, [{ include: 'source.json' }]);
});

test('both file types carry a light and a dark icon, and the gallery carries the logo', () => {
  for (const language of contributes.languages) {
    for (const variant of ['light', 'dark']) {
      const path = language.icon?.[variant];
      assert.ok(path, `${language.id} has no ${variant} icon`);
      assert.match(path, /\.svg$/, `${path} must be an SVG, so it scales in the explorer`);
      assert.ok(existsSync(here(`../${path}`)), `${path} is missing`);
    }
  }
  assert.equal(manifest.icon, 'icon.png', 'the extension logo must be a PNG — VS Code rejects SVG');
  assert.equal(manifest.galleryBanner.color, '#2b2f3a', "the app panel's fill");
  // Read the PNG header rather than trusting the extension: a file that is not really a PNG,
  // or is under 128px, leaves the extension page showing the generic placeholder.
  const png = readFileSync(here('../icon.png'));
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 'not a PNG');
  assert.equal(png.subarray(12, 16).toString('ascii'), 'IHDR');
  assert.ok(png.readUInt32BE(16) >= 128 && png.readUInt32BE(20) >= 128, 'the logo must be at least 128px');
});

test('every contributed path exists on disk', () => {
  const paths = [
    ...contributes.languages.map((l) => l.configuration),
    ...contributes.grammars.map((g) => g.path),
    ...contributes.commands.flatMap((c) => (typeof c.icon === 'object' ? Object.values(c.icon) : [])),
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
  for (const entry of contributes.menus['editor/title']) {
    const command = contributes.commands.find((c) => c.command === entry.command);
    assert.ok(command?.icon, `${entry.command} has no title-bar icon`);
    assert.ok(entry.when?.includes('resourceLangId'), `${entry.command} is not scoped to a language`);
  }
  const console = contributes.menus['editor/title'].find((e) => e.command === ids.COMMANDS.runInWebConsole);
  assert.equal(console.when, `resourceLangId == ${ids.JS_LANGUAGE_ID}`);
  const [binding] = contributes.keybindings;
  assert.equal(binding.command, ids.COMMANDS.runScript);
  assert.equal(binding.key, 'ctrl+alt+r');
  assert.equal(binding.mac, 'cmd+alt+r');
  assert.match(binding.when, new RegExp(`editorLangId == ${ids.LANGUAGE_ID}`));
});

test('every setting the code reads is declared, with its default and an explanation', () => {
  const properties = contributes.configuration.properties;
  assert.deepEqual(Object.keys(properties).sort(),
    Object.values(ids.SETTINGS).map((k) => `${ids.CONFIG_SECTION}.${k}`).sort());
  const defaults = {
    'stencil.cliPath': '', 'stencil.checkOnType': true, 'stencil.checkOnSave': true,
    'stencil.highlighting': true, 'stencil.completion': true, 'stencil.colors': {},
    'stencil.hover': true,
  };
  for (const [id, value] of Object.entries(defaults)) {
    assert.deepEqual(properties[id].default, value, `${id} has the wrong default`);
    assert.equal(properties[id].type, typeof value, `${id} has the wrong type`);
    assert.ok(properties[id].markdownDescription ?? properties[id].description,
      `${id} would sit in the settings UI with nothing said about it`);
  }
  assert.equal(properties['stencil.cliPath'].scope, 'machine-overridable',
    'a binary path belongs to the machine, and a workspace may still override it');
});

test('stencil.colors names exactly the families, and takes only a hex colour', () => {
  const { FAMILIES, HEX } = require('../src/lib/colorFamilies.js');
  const colors = contributes.configuration.properties['stencil.colors'];
  assert.deepEqual(Object.keys(colors.properties).sort(), [...FAMILIES].sort(),
    'the settings UI drifted from src/lib/colorFamilies.js');
  assert.equal(colors.additionalProperties, false, 'a mistyped family is dropped in silence');
  const [[named, schema]] = Object.entries(colors.patternProperties);
  const isFamily = new RegExp(named);
  for (const family of FAMILIES) assert.ok(isFamily.test(family), `${family} is unvalidated`);
  assert.ok(!isFamily.test('nonsense'), 'the alternation is anchored to the families');
  const accepted = new RegExp(schema.pattern);
  for (const value of ['#abc', '#abcd', '#aabbcc', '#aabbccdd', '', 'red', '#ab', '#12345']) {
    assert.equal(accepted.test(value), value === '' || HEX.test(value),
      `the schema and colorFamilies.js disagree about ${value || '""'}`);
  }
});

test('the README settings table lists exactly the declared settings', () => {
  const readme = readFileSync(here('../README.md'), 'utf8');
  const listed = [...readme.matchAll(/^\| `(stencil\.\w+)` \|/gm)].map(([, id]) => id);
  assert.deepEqual(listed.sort(),
    Object.keys(contributes.configuration.properties).sort(), 'the README drifted');
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
  for (const line of ['icons', 'icon.png', 'README.md']) {
    assert.ok(!new RegExp(`^${line}`, 'm').test(ignore), `${line} must ship — it is the extension page`);
  }
});
