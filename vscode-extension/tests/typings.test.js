// The facade's types as a file a workspace can hold, written into a project by one command.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createRequire } from 'node:module';

import { buildTypings, DOCS_URL } from '../tools/genTypings.mjs';
import { installVscodeStub, makeDocument, makeEditor, makeVscode } from './helpers/vscodeStub.js';

const require = createRequire(import.meta.url);
const file = require('../src/lib/typingsFile.js');
const TABLE = require('../src/config/stencilApiVocabulary.json');

const COMMITTED = readFileSync(new URL('../typings/stencil.d.ts', import.meta.url), 'utf8');

// Awaited: a finally around an un-awaited promise deletes the directory under the body.
const withDir = async (body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-typings-'));
  try {
    return await body(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
};

test('the committed types are what the generator makes of the facade, byte for byte', () => {
  assert.equal(COMMITTED, buildTypings(), 'run: node tools/genTypings.mjs');
});

// A top-level `export` would make it a MODULE, and its declarations would stop being global.

test('it is an ambient script, and it declares the global', () => {
  assert.ok(!/^export /m.test(COMMITTED), 'a top-level export would make it a module');
  // `var`, the way lib.dom.d.ts declares `window` — a `const` takes the read-only colour.
  assert.match(COMMITTED, /^declare var stencil: Stencil;$/m);
  assert.match(COMMITTED, /^interface Stencil extends StencilSettings \{$/m);
  // The app's own imports become opaque names, so eight more modules are not dragged in.
  assert.match(COMMITTED, /^type DrawingApp = unknown;$/m);
  assert.ok(!/^import /m.test(COMMITTED));
});

test('every member arrives with its prose, its example and a link to the docs', () => {
  for (const [name, entry] of Object.entries(TABLE.members)) {
    assert.ok(COMMITTED.includes(`   * ${entry.summary}`), `${name} lost its summary`);
    const [firstLine] = entry.example.split('\n');
    assert.ok(COMMITTED.includes(`   * ${firstLine}`), `${name} lost its example`);
  }
  // The link renders the way lib.dom.d.ts's "MDN Reference" does — that is what it is for.
  assert.ok(COMMITTED.includes(`[Stencil console API](${DOCS_URL})`));
  assert.equal(COMMITTED.split('[Stencil console API]').length - 1,
    Object.keys(TABLE.members).length + 1, 'every member, and the global itself');
});

test('installing writes the types, and a jsconfig only where the project has none', async () => {
  await withDir((dir) => {
    assert.equal(file.installedIn(dir), false);
    const written = file.install(dir);
    assert.equal(written.typings, join(dir, 'stencil.d.ts'));
    assert.equal(readFileSync(written.typings, 'utf8'), COMMITTED);
    assert.equal(written.config, join(dir, 'jsconfig.json'));
    assert.match(readFileSync(written.config, 'utf8'), /"include"/);
    assert.equal(file.installedIn(dir), true);
  });
  await withDir((dir) => {
    // A config the project already has is the user's, and is left alone.
    const mine = join(dir, 'jsconfig.json');
    writeFileSync(mine, '{ "include": ["src"] }\n');
    const written = file.install(dir);
    assert.equal(written.config, '', 'no config was written');
    assert.equal(readFileSync(mine, 'utf8'), '{ "include": ["src"] }\n');
  });
  await withDir((dir) => {
    writeFileSync(join(dir, 'tsconfig.json'), '{}\n');
    assert.equal(file.hasJsConfig(dir), true);
    assert.equal(file.install(dir).config, '');
  });
  assert.equal(file.installedIn(''), false);
});

const withHost = async (options, body) => {
  const { vscode, calls } = makeVscode(options);
  const host = installVscodeStub(vscode);
  try {
    return await body({ calls, vscode, typings: host.require('typings.js') });
  } finally {
    host.restore();
  }
};

test('the command writes into the open document\'s folder and wakes the language service', async () => {
  await withDir(async (dir) => {
    await withHost({ workspaceFolder: dir }, async ({ calls, vscode, typings }) => {
      vscode.window.activeTextEditor = makeEditor(makeDocument({
        path: join(dir, 'a.js'), languageId: 'javascript', text: '// @use stencil\n',
      }));
      const written = await typings.addTypings();
      assert.equal(readFileSync(written.typings, 'utf8'), COMMITTED);
      // Without the restart the service keeps the answer it already decided on.
      assert.deepEqual(calls.executed.map((c) => c.id), ['typescript.restartTsServer']);
      assert.match(calls.infos.at(-1), /stencil\.d\.ts/);
      assert.deepEqual(calls.errors, []);
    });
  });
});

test('with no folder open it refuses, and writes nothing', async () => {
  await withHost({}, async ({ calls, typings }) => {
    assert.equal(await typings.addTypings(), undefined);
    assert.equal(calls.errors.at(-1), typings.NO_FOLDER);
    assert.deepEqual(calls.executed, []);
  });
});
