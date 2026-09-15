// The facade vocabulary is this surface's prose over somebody else's list. The list is
// browser/js/console/stencilApi.d.ts — the one written-down surface of window.stencil — and
// it is asserted BOTH ways, so a new facade member is unexplained until it is written down
// and a documented non-member cannot survive a rename.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const api = require('../src/lib/apiVocabulary.js');
const TABLE = require('../src/config/stencilApiVocabulary.json');

const DTS = readFileSync(new URL('../../browser/js/console/stencilApi.d.ts', import.meta.url), 'utf8');

// The body of one interface: from its declaration to the first line that closes it.
const body = (pattern) => {
  const match = pattern.exec(DTS);
  assert.ok(match, `${pattern} is not in stencilApi.d.ts`);
  const open = DTS.indexOf('{', match.index);
  return DTS.slice(open + 1, DTS.indexOf('\n}', open));
};

// `name(args): T` or `name: T`, with `readonly` and a doc comment in front of either.
const MEMBER = /^(?:(readonly)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\??\s*[(:]/;

const membersOf = (text) => {
  const out = new Map();
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('//') || line.startsWith('/*') || line.startsWith('*')) continue;
    const match = MEMBER.exec(line);
    if (match) out.set(match[2], { readOnly: !!match[1], line: line.replace(/;\s*$/, '') });
  }
  return out;
};

const facade = new Map([
  ...membersOf(body(/export interface StencilSettings/g)),
  ...membersOf(body(/export interface Stencil extends/g)),
]);

test('the .d.ts really is the surface — it parses to a facade, not to nothing', () => {
  assert.ok(facade.size > 100, `only ${facade.size} members parsed out of stencilApi.d.ts`);
  for (const name of ['execScript', 'load', 'crop', 'rotateRight', 'lineColor', 'imageSize']) {
    assert.ok(facade.has(name), `${name} should be in the parsed facade`);
  }
});

test('every facade member is explained, and every explained member is a facade member', () => {
  assert.deepEqual([...api.MEMBER_NAMES].sort(), [...facade.keys()].sort());
});

test('each signature is the one the facade declares, with `readonly` kept as a flag', () => {
  for (const [name, declared] of facade) {
    const entry = api.entryFor(name);
    assert.equal(entry.signature, `stencil.${declared.line.replace(/^readonly\s+/, '')}`,
      `${name}'s signature drifted from stencilApi.d.ts`);
    assert.equal(!!entry.readOnly, declared.readOnly, `${name}: readonly drifted`);
  }
});

test('every entry carries the prose a hint is made of', () => {
  const GROUPS = new Set(['settings', 'state', 'projects', 'servers', 'assistant', 'windows',
    'edit', 'export', 'session', 'script', 'geometry']);
  for (const name of api.MEMBER_NAMES) {
    const entry = api.entryFor(name);
    assert.ok(GROUPS.has(entry.group), `${name} is in no known group (${entry.group})`);
    assert.match(entry.summary, /\.$/, `${name}'s summary should read as a sentence`);
    assert.ok(entry.summary.length > 12, `${name}'s summary says nothing`);
    assert.ok(entry.signature.startsWith(`stencil.${name}`), `${name}'s signature names another member`);
    assert.ok(entry.example, `${name} has no worked example`);
    assert.ok(entry.example.includes(name), `${name}'s example does not use it`);
  }
});

test('an example is real JavaScript, and a call says what it hands back', () => {
  for (const name of api.MEMBER_NAMES) {
    const entry = api.entryFor(name);
    // Parsed, not eyeballed: a broken snippet in a tooltip teaches the wrong thing.
    assert.doesNotThrow(() => new Function(`async () => {\n${entry.example}\n}`),
      `${name}'s example does not parse`);
    if (/\): Stencil$/.test(entry.signature)) assert.match(entry.detail, /chain/);
    if (/\): Promise<Stencil>$/.test(entry.signature)) assert.match(entry.detail, /await/);
  }
  assert.ok(TABLE._doc.includes('stencilApi.d.ts'), 'the table says where its list comes from');
});

test('explain renders a member as Markdown, fenced as JavaScript', () => {
  const markdown = api.explain('crop');
  assert.match(markdown, /^\*\*stencil\.crop\*\* — /);
  assert.match(markdown, /```js\nstencil\.crop\(spec\?: CropSpec\): Stencil\n```/);
  assert.equal(api.explain('nosuchmember'), '');
  // A read-only member says so; a writable one does not.
  assert.match(api.explain('imageSize'), /Read-only\.$/);
  assert.ok(!api.explain('thickness').includes('Read-only'));
  assert.match(api.explain('execScript'), /Run a \.stc script against the open project/,
    'a doc comment in the .d.ts becomes the detail paragraph');
});

test('prefixAt answers only where a member really follows the facade', () => {
  assert.equal(api.prefixAt('stencil.'), '');
  assert.equal(api.prefixAt('  await window.stencil.rot'), 'rot');
  assert.equal(api.prefixAt('const s = window . stencil . crop'), 'crop');
  for (const line of ['myStencil.', 'other.stencil', 'stencil', '', 'stencil.crop().', 'a.b.']) {
    assert.equal(api.prefixAt(line), null, `${line} is not a member position`);
  }
});

test('memberAt names the member the caret is inside, and only through the facade', () => {
  assert.equal(api.memberAt('stencil.rotateRight()', 10), 'rotateRight');
  assert.equal(api.memberAt('stencil.rotateRight()', 8), 'rotateRight', 'its first character counts');
  assert.equal(api.memberAt('await stencil.load(url)', 15), 'load');
  assert.equal(api.memberAt('other.rotateRight()', 8), '', 'somebody else\'s object');
  assert.equal(api.memberAt('stencil.nosuchmember', 10), '');
  assert.equal(api.memberAt('stencil.crop()', 3), '', 'the facade itself is not a member');
});

// The editor's JavaScript service sees a name nothing declares and can only say `any`.
test('facadeAt finds the global, bare or on window, and never somebody else\'s', () => {
  assert.equal(api.facadeAt('stencil.crop()', 3), true);
  assert.equal(api.facadeAt('await window.stencil.load(url)', 16), true);
  assert.equal(api.facadeAt('window . stencil . crop()', 11), true);
  for (const [line, at] of [['const myStencil = 1', 9], ['other.stencil', 8], ['stencil.crop()', 10],
    ['const s = stencils', 13], ['', 0]]) {
    assert.equal(api.facadeAt(line, at), false, `${line} @${at}`);
  }
});

test('the global\'s explanation says what it is, and where it exists', () => {
  assert.match(api.FACADE_DOC, /^\*\*`stencil`\*\* — /);
  assert.match(api.FACADE_DOC, /```js\n/);
  assert.match(api.FACADE_DOC, /Run in Stencil Web Console/);
});
