// The desktop hand-off link (js/core/launch/desktopLink.js): a server reference or the picture
// inline, a script beside either or alone, and the OS launch limits it is measured against.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { desktopLaunchUrl, inlineVerdict, INLINE_MAX_CHARS, INLINE_WARN_CHARS } from '../../../js/core/launch/desktopLink.js';

const server = { server: { url: 'http://h:1', id: 'p', version: 3 } };
const inline = { dataUrl: 'data:image/png;base64,AA', layout: { lines: [] } };

test('a server project goes by reference, a local one inline, a script beside either', () => {
  assert.equal(desktopLaunchUrl({ payload: server }), 'stencil://open?server=http%3A%2F%2Fh%3A1&id=p&version=3');
  assert.equal(desktopLaunchUrl({ scheme: 'stx', payload: { ...inline, incognito: true } }),
    'stx://open?src=data%3Aimage%2Fpng%3Bbase64%2CAA&layout=%7B%22lines%22%3A%5B%5D%7D&incognito=1');
  assert.equal(desktopLaunchUrl({ payload: server, script: '@save', scriptMode: 'open' }),
    'stencil://open?server=http%3A%2F%2Fh%3A1&id=p&version=3&script=%40save&scriptMode=open');
});

test('with nothing open, the script goes alone and still carries incognito', () => {
  assert.equal(desktopLaunchUrl({ payload: null, incognito: true, script: '@crop 10%' }),
    'stencil://open?incognito=1&script=%40crop%2010%25&scriptMode=run');
});

test('only an inline link is measured: a bare server reference always fits', () => {
  assert.equal(inlineVerdict('x'.repeat(INLINE_MAX_CHARS + 1), { payload: server }), 'ok');
  assert.equal(inlineVerdict('x'.repeat(INLINE_MAX_CHARS + 1), { payload: server, script: '@save' }), 'refuse');
  assert.equal(inlineVerdict('x'.repeat(INLINE_WARN_CHARS + 1), { payload: inline }), 'warn');
  assert.equal(inlineVerdict('x'.repeat(10), { payload: null, script: '@save' }), 'ok');
});
