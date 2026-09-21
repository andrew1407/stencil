// The `#stencil=` hand-off: the URL shape the browser app boots on, and what a .stc, a
// .stencil or a picture becomes inside it. The receiving contract is browser/js/core/
// deepLink.js, whose vectors pin that a `script` key rides through untouched.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const web = require('../src/lib/web/webLaunch.js');

// A real 1×1 PNG, so a round trip through base64 is a round trip through real bytes.
const PNG = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==', 'base64');

const withDir = (body) => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-web-'));
  try {
    return body(dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
};

const payloadOf = (url) => JSON.parse(decodeURIComponent(url.slice(url.indexOf('#stencil=') + 9)));

test('the URL is base + #stencil= + the encoded payload, and replaces any fragment there', () => {
  const url = web.buildLaunchUrl('https://app.example/', { script: '@crop 10%\n' });
  assert.ok(url.startsWith('https://app.example/#stencil='));
  assert.deepEqual(payloadOf(url), { script: '@crop 10%\n' });
  // An instance that arrived with a fragment must not end up with two.
  const twice = web.buildLaunchUrl('https://app.example/#stencil=old', { script: 'x' });
  assert.equal(twice.split('#').length, 2);
});

test('a script alone is the whole payload — a picture is the script\'s own business', () => {
  assert.deepEqual(web.scriptLaunch('@filter sepia\n'), { script: '@filter sepia\n' });
});

test('a remote image is named for the app to fetch; a local one is inlined', () => {
  withDir((dir) => {
    const path = join(dir, 'cat.png');
    writeFileSync(path, PNG);
    assert.deepEqual(web.imagePart('https://cdn.example/a/b.png'),
      { src: 'https://cdn.example/a/b.png', name: 'b.png' });
    const local = web.imagePart(path);
    assert.equal(local.name, 'cat.png');
    assert.ok(local.dataUrl.startsWith('data:image/png;base64,'));
    assert.deepEqual(Buffer.from(local.dataUrl.split(',')[1], 'base64'), PNG);
  });
});

test('inlining off keeps local bytes out of the URL, and never touches a remote one', () => {
  withDir((dir) => {
    const path = join(dir, 'cat.png');
    writeFileSync(path, PNG);
    assert.equal(web.imagePart(path, { inline: false }), null);
    assert.ok(web.imagePart('https://cdn.example/b.png', { inline: false }).src);
  });
});

test('a file that is not an image, or is not there at all, inlines as nothing', () => {
  withDir((dir) => {
    const notes = join(dir, 'notes.txt');
    writeFileSync(notes, 'hello');
    assert.equal(web.imageDataUrl(notes), null, 'an unknown extension names no image type');
    assert.equal(web.imageDataUrl(join(dir, 'gone.png')), null);
    assert.equal(web.imageDataUrl(''), null);
  });
});

test('a remote spec that will not parse names nothing, rather than throwing', () => {
  assert.equal(web.imagePart('https://'), null);
  assert.equal(web.imagePart('http://[oops'), null);
  assert.deepEqual(web.scriptLaunch('@crop 10%\n', 'https://'), { script: '@crop 10%\n' });
  assert.equal(web.imagePart('https://cdn.example/').name, 'image.png', 'a URL with no file part');
});

test('localSources names what the browser cannot open, and nothing it can', () => {
  const blocks = [
    { source: 'https://cdn.example/i.png', kind: 'url' },
    { source: './cat.png', kind: 'file' },
    { source: 'shots/*.png', kind: 'glob' },
    { source: 'shots/', kind: 'dir' },
    { source: '', kind: 'file' },
  ];
  assert.deepEqual(web.localSources(blocks), ['./cat.png', 'shots/*.png', 'shots/']);
  assert.deepEqual(web.localSources(undefined), []);
});

test('a .stencil becomes the fragment\'s own parts: its image and its layout', () => {
  const doc = {
    format: 'stencil-project', version: 1, name: 'roof',
    image: { dataUrl: 'data:image/png;base64,AAAA', ext: '.png' },
    layout: { imageWidth: 10, imageHeight: 20, lines: [] },
  };
  assert.deepEqual(web.projectLaunch(JSON.stringify(doc)), {
    dataUrl: 'data:image/png;base64,AAAA', name: 'roof.png',
    layout: { imageWidth: 10, imageHeight: 20, lines: [] },
  });
});

test('anything that is not a project with an inline image is not a hand-off', () => {
  assert.equal(web.projectLaunch('{ not json'), null);
  assert.equal(web.projectLaunch('null'), null);
  assert.equal(web.projectLaunch('{}'), null);
  assert.equal(web.projectLaunch(JSON.stringify({ image: {} })), null);
  // A project pointing OUT at a URL is not one the fragment can carry as bytes.
  assert.equal(web.projectLaunch(JSON.stringify({ image: { dataUrl: 'https://cdn.example/i.png' } })), null);
});

test('the cap is Chrome\'s navigation ceiling, well under the validator\'s 32 MiB', () => {
  assert.equal(web.MAX_PAYLOAD, 1_800_000);
  // The Chrome extension writes the same fragment, so it holds the same number.
  const twin = readFileSync(new URL('../../browser-extension/src/lib/menu/editorLaunch.js', import.meta.url), 'utf8');
  const declared = /const MAX_PAYLOAD = ([0-9_]+);/.exec(twin);
  assert.ok(declared, 'editorLaunch.js no longer declares MAX_PAYLOAD');
  assert.equal(Number(declared[1].replaceAll('_', '')), web.MAX_PAYLOAD);
  assert.equal(web.isTooBig('x'.repeat(web.MAX_PAYLOAD)), false);
  assert.equal(web.isTooBig('x'.repeat(web.MAX_PAYLOAD + 1)), true);
  const huge = web.buildLaunchUrl('https://app.example/', { script: '#'.repeat(web.MAX_PAYLOAD) });
  assert.equal(web.isTooBig(huge), true, 'the WHOLE url is what a browser refuses');
});

test('the payload survives the receiving codec: a script rides beside a picture', () => {
  withDir((dir) => {
    const path = join(dir, 'cat.png');
    writeFileSync(path, PNG);
    const payload = web.scriptLaunch('@crop 10%\n', path);
    const back = payloadOf(web.buildLaunchUrl('https://app.example/', payload));
    assert.equal(back.script, '@crop 10%\n');
    assert.equal(back.name, 'cat.png');
    assert.ok(back.dataUrl.startsWith('data:image/png;base64,'));
  });
});
