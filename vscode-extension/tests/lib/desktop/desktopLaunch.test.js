// The `stencil://` link the desktop hand-off opens (src/lib/desktop/launch.js): byte for byte what
// the browser app's buildStencilSchemeUrl writes for the same parts, under the browser's own cap.
import test from 'node:test';
import assert from 'node:assert/strict';

import { DESKTOP_SCHEME, MAX_DESKTOP_LINK, buildDesktopUrl, desktopLaunch, isTooBigForDesktop,
} from '../../../src/lib/desktop/launch.js';
import { buildStencilSchemeUrl } from '../../../../browser/js/core/launch/deepLink.js';
import { INLINE_MAX_CHARS } from '../../../../browser/js/core/launch/desktopLink.js';

const PARTS = [
  { script: '@crop 10%\n', scriptMode: 'open' },
  { script: '@filter bw\n', scriptMode: 'run', incognito: true },
  { script: '@filter bw\n', scriptMode: 'bogus' },
  { src: 'data:image/png;base64,AAAA', layout: { lines: [] }, script: 'a & b = c?', scriptMode: 'run' },
  { src: 'https://cdn.example/i.png', incognito: true },
  {},
];

for (const parts of PARTS) {
  test(`the link matches the browser's for ${JSON.stringify(parts)}`, () => {
    assert.equal(buildDesktopUrl(parts), buildStencilSchemeUrl({ scheme: DESKTOP_SCHEME, ...parts }));
  });
}

test('a script-only link is valid, and an unknown mode runs', () => {
  assert.equal(buildDesktopUrl({ script: 'x', scriptMode: 'open' }), 'stencil://open?script=x&scriptMode=open');
  assert.match(buildDesktopUrl({ script: 'x', scriptMode: 'later' }), /scriptMode=run$/);
});

test('a hand-off payload rides as src: a picked picture as its data URL, a named one as its URL', () => {
  assert.match(desktopLaunch({ script: 's', dataUrl: 'data:image/png;base64,AA' }, { mode: 'open' }),
    /^stencil:\/\/open\?src=data%3Aimage%2Fpng%3Bbase64%2CAA&script=s&scriptMode=open$/);
  assert.match(desktopLaunch({ script: 's', src: 'https://cdn.example/i.png' }, { incognito: true }),
    /src=https%3A%2F%2Fcdn\.example%2Fi\.png&incognito=1&script=s&scriptMode=run$/);
});

test('the cap is the browser\'s own for an inline desktop link', () => {
  assert.equal(MAX_DESKTOP_LINK, INLINE_MAX_CHARS);
  assert.equal(isTooBigForDesktop('x'.repeat(MAX_DESKTOP_LINK)), false);
  assert.equal(isTooBigForDesktop('x'.repeat(MAX_DESKTOP_LINK + 1)), true);
});
