// manifest.json's two security surfaces: the extension-pages CSP, and how much of the
// extension a WEB PAGE may load. Only pages a page context actually navigates to belong
// in web_accessible_resources — everything listed there is reachable (and fingerprintable)
// from any site the user visits.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';

const here = (rel) => new URL(rel, import.meta.url);
const MANIFEST = JSON.parse(readFileSync(here('../manifest.json'), 'utf8'));
const war = MANIFEST.web_accessible_resources;
const exposed = war.flatMap((e) => e.resources);

const csp = MANIFEST.content_security_policy.extension_pages;
const directives = Object.fromEntries(csp.split(';').map((d) => d.trim()).filter(Boolean)
  .map((d) => { const [name, ...values] = d.split(/\s+/); return [name, values]; }));

test('extension pages declare a CSP, and it grants no script escape', () => {
  assert.deepEqual(directives['script-src'], ["'self'"]);
  assert.deepEqual(directives['object-src'], ["'none'"]);
  for (const escape of ["'unsafe-eval'", "'unsafe-inline'", 'wasm-unsafe-eval', '*', 'https:']) {
    assert.ok(!directives['script-src'].includes(escape), `script-src must not grant ${escape}`);
  }
  // The popup lists page images by URL and talks to user-configured LLM endpoints.
  assert.ok(directives['style-src'].includes("'unsafe-inline'"));
  for (const s of ['data:', 'blob:', 'https:']) assert.ok(directives['img-src'].includes(s), `img-src ${s}`);
  for (const s of ['https:', 'wss:']) assert.ok(directives['connect-src'].includes(s), `connect-src ${s}`);
});

// The only page a web page ever loads from this extension is the quick-crop modal; its own
// subresources load from the chrome-extension:// document, so they need no entry.
test('web_accessible_resources exposes only the in-page crop modal', () => {
  assert.deepEqual(exposed, ['src/crop/crop.html']);
});

// NEGATIVE: the library, the icons and the other extension pages must NOT be reachable
// from a page. src/lib/* alone was 70-odd modules any site could pull and fingerprint.
test('nothing else in the extension is web-reachable', () => {
  const wide = ['src/lib/*', 'src/lib/', 'icons/*', 'src/crop/*', 'src/popup/', 'src/options/', '*'];
  for (const pat of wide) assert.ok(!exposed.includes(pat), `${pat} must not be web-accessible`);
  const libModules = readdirSync(here('../src/lib')).filter((f) => f.endsWith('.js'));
  assert.ok(libModules.length > 20);
  for (const f of libModules) {
    assert.ok(!exposed.some((r) => r === `src/lib/${f}` || r.startsWith('src/lib/')), `src/lib/${f} is exposed`);
  }
});

// Anything a page CAN load has to be something a page is meant to load: every entry must
// be a real file, and the only chrome.runtime.getURL() targets are the two extension pages.
test('every exposed resource exists, and the getURL targets are accounted for', () => {
  for (const rel of exposed) assert.ok(readFileSync(here(`../${rel}`), 'utf8').length, rel);
  const stencil = readFileSync(here('../src/lib/stencil.js'), 'utf8');
  assert.ok(stencil.includes("chrome.runtime.getURL('src/crop/crop.html')"));
});
