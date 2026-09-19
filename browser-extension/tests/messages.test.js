// lib/messages.js — the cross-context message table. Nothing at runtime enforces that the
// classic content scripts and injected functions (which cannot import) spell a channel the
// same way the module does; a typo simply drops the message in silence. This is that guard,
// plus the shape rules the table itself promises.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { MSG, SRC } from '../src/lib/messages.js';

const read = (rel) => readFileSync(new URL(`../src/${rel}`, import.meta.url), 'utf8');
const ALL = { ...MSG, ...SRC };

test('every channel is a distinct, namespaced, lower-kebab string', () => {
  const seen = new Map();
  for (const [name, value] of Object.entries(ALL)) {
    assert.match(value, /^stencil-[a-z-]+$/, `${name} is not a namespaced channel`);
    assert.equal(seen.has(value), false, `${value} is both ${seen.get(value)} and ${name}`);
    seen.set(value, name);
  }
  // The two halves never share a value either — a runtime `type` and a postMessage
  // `source` tag are read in different places and must not be confusable.
  for (const value of Object.values(SRC)) assert.equal(Object.values(MSG).includes(value), false);
});

// Files that cannot import the module keep a MIRROR. Each entry is the file and the
// channel names its mirror declares; the values must be the table's, to the byte.
const MIRRORS = [
  ['content/ctxTarget.js', ['WAKE', 'CTX'], []],
  ['content/pageApiBridge.js',
    ['PAGE_SET_FILTERS', 'PAGE_REQUEST_SYNC'],
    ['PAGE_API', 'PAGE_FILTERS', 'PAGE_PINS', 'PAGE_EDITED', 'PAGE_HL_COLOR']],
  ['content/pageApiMain.js',
    ['PAGE_OPEN', 'PAGE_CROP', 'PAGE_PIN', 'PAGE_REQUEST_SYNC', 'PAGE_DISABLE', 'PAGE_SET_FILTERS'],
    ['PAGE_API', 'PAGE_FILTERS', 'PAGE_PINS', 'PAGE_EDITED', 'PAGE_HL_COLOR']],
  ['content/editorApiMain.js',
    ['EDITOR_LIST', 'EDITOR_STATE', 'EDITOR_IMPORT', 'EDITOR_SWITCH_PROJECT', 'EDITOR_FOCUS_TAB',
     'SOURCE_TABS', 'SCAN_TAB', 'PAGE_OPEN', 'PAGE_CROP'],
    ['EXT_API', 'EXT_API_RES']],
  ['content/editorBridge.js', ['REGISTRY', 'EDITOR_SWITCH', 'EDITOR_STATE', 'EDITOR_IMPORT',
    'EDITOR_SWITCH_PROJECT', 'EDITOR_CROP'], ['EXT_REQ', 'EXT_RES', 'EXT_API', 'EXT_API_RES']],
];

for (const [file, msgKeys, srcKeys] of MIRRORS) {
  test(`${file}: its mirror spells every channel the way the table does`, () => {
    const text = read(file);
    assert.ok(text.includes('mirror of lib/messages.js'), 'the mirror says what it mirrors');
    for (const [keys, table] of [[msgKeys, MSG], [srcKeys, SRC]]) {
      for (const key of keys) {
        assert.ok(table[key], `${key} is no longer in the table — the mirror is stale`);
        assert.ok(text.includes(`${key}: '${table[key]}'`),
          `${file}: ${key} must be mirrored as '${table[key]}'`);
      }
    }
    // …and the other way round: EVERY pair the mirror declares must be the table's, so a
    // channel added to a mirror without being listed above still can't drift or misspell.
    for (const [, key, value] of text.matchAll(/(\w+): '(stencil-[a-z-]+)'/g)) {
      assert.ok(ALL[key], `${file}: ${key} is not a channel in lib/messages.js`);
      assert.equal(value, ALL[key], `${file}: ${key} must be mirrored as '${ALL[key]}'`);
    }
  });
}

// Two injected functions carry a channel as a bare literal — they send one message each — but the
// literal still has to be the table's.
test('the injected one-shot senders use the table\'s literal', () => {
  assert.ok(read('lib/highlight.js').includes(`const HL_HOVER = '${MSG.HL_HOVER}'`));
  assert.ok(read('lib/dropZones.js').includes(`type: '${MSG.PAGE_DROP}'`));
  assert.ok(read('lib/overlay.js').includes(`type: '${MSG.OPEN_TAB}'`));
  assert.ok(read('lib/overlay.js').includes(`d.source !== '${SRC.MODAL}'`));
});

// NEGATIVE: a module that CAN import must not hand-roll a channel — that is precisely
// the drift the table exists to prevent.
test('no importing module writes a channel literal instead of using the table', () => {
  const MIRRORED = new Set(MIRRORS.map(([f]) => `src/${f}`)
    .concat(['src/lib/messages.js', 'src/lib/highlight.js', 'src/lib/dropZones.js', 'src/lib/overlay.js']));
  const values = new Set(Object.values(ALL));
  const offenders = [];
  for (const file of ['lib/contextMenu.js', 'lib/stencil.js', 'lib/connections.js',
    'background/background.js', 'background/menus.js', 'background/registrars.js',
    'background/editorRelay.js', 'background/ctxActions.js', 'popup/popup.js',
    'popup/editorMode.js', 'options/options.js', 'crop/crop.js']) {
    if (MIRRORED.has(`src/${file}`)) continue;
    for (const m of read(file).matchAll(/'(stencil-[a-z-]+)'/g))
      if (values.has(m[1])) offenders.push(`${file}: '${m[1]}'`);
  }
  assert.deepEqual(offenders, [], 'import MSG/SRC from lib/messages.js instead');
});

// The header's own promise: the editor-mode group is the request/response traffic, and
// every one of those handlers answers rather than rejecting.
test('every editor-mode channel is registered by a handler that answers', () => {
  const handlers = ['background/handlers/editorMode.js', 'background/handlers/ctxProbe.js',
    'background/handlers/dropZones.js', 'background/handlers/pageApi.js'].map(read).join('\n');
  for (const key of ['EDITOR_LIST', 'EDITOR_IMPORT', 'EDITOR_SWITCH_PROJECT', 'EDITOR_FOCUS_TAB',
    'SOURCE_TABS', 'SCAN_TAB']) {
    assert.ok(handlers.includes(`[MSG.${key}]: answers(`),
      `${key} must be registered through answers() — every leg waits on a reply`);
  }
});
