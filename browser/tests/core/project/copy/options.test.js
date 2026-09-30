// settleCopyOptions (core/project/copy/options.js): what a copy request reduces to.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { settleCopyOptions } from '../../../../js/core/project/copy/options.js';

const CASES = [
  ['a plain local copy', { what: 'layout' }, {}, { open: 'none', incognito: false, onServer: false, note: null }],
  ['a server source copies onto its server', { what: 'image' }, { serverSource: true }, { onServer: true, note: null }],
  ['local keeps a server source local', { what: 'image', local: true }, { serverSource: true }, { onServer: false }],
  ['local on a local source changes nothing', { what: 'image', local: true }, {}, { onServer: false, note: null }],
  ['incognito opened here stays', { what: 'project', open: 'here', incognito: true }, {}, { incognito: true, note: null }],
  ['incognito unopened is dropped', { what: 'project', incognito: true }, {},
    { incognito: false, note: 'an incognito copy must be opened — saved it instead' }],
  ['incognito on a server copy is dropped', { what: 'layout', open: 'newtab', incognito: true }, { serverSource: true },
    { incognito: false, onServer: true, note: 'a server copy cannot be incognito — made it on the server' }],
  ['incognito on a local copy of a server source stays', { what: 'layout', open: 'newtab', incognito: true, local: true },
    { serverSource: true }, { incognito: true, onServer: false, note: null }],
];

for (const [name, req, ctx, want] of CASES) {
  test(`settleCopyOptions: ${name}`, () => {
    const got = settleCopyOptions(req, ctx);
    for (const [k, v] of Object.entries(want)) assert.deepEqual(got[k], v, `${name}: ${k}`);
  });
}

test('settleCopyOptions: an unknown scope or open throws', () => {
  assert.throws(() => settleCopyOptions({ what: 'lines' }), /Unknown copy scope "lines"/);
  assert.throws(() => settleCopyOptions({ what: 'image', open: 'window' }), /Unknown copy open "window"/);
});
