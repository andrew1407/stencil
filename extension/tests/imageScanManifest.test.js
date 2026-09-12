// lib/imageScan.js runs INJECTED in the scanned page and pulls its web-app manifest with
// the page's cookies. The href is page-supplied, so that credentialed fetch may only ever
// reach the page's OWN origin — lib/urlGuard.js is the guard everywhere else, but an
// injected function can't import, and same-origin is tighter than its same-host carve-out.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scanPageForImages } from '../src/lib/imageScan.js';
import { installDom, stubDoc } from './helpers/domStub.js';

const scan = async (manifestHref, pageUrl = 'https://shop.example/cart') => {
  const fetched = [];
  const link = { getAttribute: (k) => (k === 'href' ? manifestHref : null), crossOrigin: null };
  const restore = installDom({
    document: stubDoc({ querySelector: (sel) => (sel.includes('manifest') ? link : null) }),
    location: new URL(pageUrl),
    fetch: async (url, opts) => {
      fetched.push({ url, credentials: opts && opts.credentials });
      return { json: async () => ({ icons: [{ src: '/icon.png' }] }) };
    },
  });
  try {
    return { items: await scanPageForImages(1000), fetched };
  } finally { restore(); }
};

// NEGATIVE: a page pointing its manifest link anywhere off its own origin gets no fetch.
test('a cross-origin or internal manifest href is never fetched', async () => {
  for (const href of ['https://evil.example/m.json', 'http://127.0.0.1:9200/_all',
    'http://169.254.169.254/latest/meta-data/', 'http://10.0.0.5/m.json', '//evil.example/m.json']) {
    const { items, fetched } = await scan(href);
    assert.deepEqual(fetched, [], href);
    assert.deepEqual(items, [], href);
  }
});

test('the page\'s own manifest is still read, and its icons listed', async () => {
  const { items, fetched } = await scan('/site.webmanifest');
  assert.equal(fetched.length, 1);
  assert.equal(fetched[0].url, 'https://shop.example/site.webmanifest');
  assert.equal(fetched[0].credentials, 'include');
  assert.deepEqual(items.map((i) => i.src), ['https://shop.example/icon.png']);
});

test('a scheme-relative href that HAPPENS to resolve same-origin is still fine', async () => {
  const { fetched } = await scan('//shop.example/m.json');
  assert.equal(fetched.length, 1);
});
