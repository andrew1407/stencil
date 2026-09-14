// The app's Content-Security-Policy — the <meta> in index.html and the identical nginx
// response header. It has to allow what the app really does (wasm, ES modules from
// 'self', style="" attributes in the markup templates, data:/blob: media, and the
// USER-CONFIGURED LLM / collaboration endpoints) and nothing looser than that.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { CSP_META } from '../tools/singleFilePatterns.js';

const read = (f) => readFileSync(new URL(`../${f}`, import.meta.url), 'utf8');
const HTML = read('index.html');
const NGINX = read('nginx.conf');

const meta = HTML.match(/<meta http-equiv="Content-Security-Policy" content="([^"]+)">/);
const POLICY = meta ? meta[1] : '';
const directives = Object.fromEntries(POLICY.split(';').map((d) => d.trim()).filter(Boolean)
  .map((d) => { const [name, ...values] = d.split(/\s+/); return [name, values]; }));

test('index.html ships a policy', () => {
  assert.ok(meta, 'no <meta http-equiv="Content-Security-Policy"> in index.html');
  assert.deepEqual(directives['default-src'], ["'self'"]);
  assert.deepEqual(directives['object-src'], ["'none'"]);
  assert.deepEqual(directives['base-uri'], ["'self'"]);
  assert.deepEqual(directives['form-action'], ["'self'"]);
});

test('it allows what the app needs', () => {
  assert.deepEqual(directives['script-src'], ["'self'", "'wasm-unsafe-eval'"]);
  assert.ok(directives['style-src'].includes("'unsafe-inline'"));
  for (const s of ['data:', 'blob:']) assert.ok(directives['img-src'].includes(s), `img-src ${s}`);
  for (const s of ['https:', 'ws:', 'wss:']) assert.ok(directives['connect-src'].includes(s), `connect-src ${s}`);
  assert.deepEqual(directives['worker-src'], ["'self'"]);
  assert.deepEqual(directives['manifest-src'], ["'self'"]);
});

// NEGATIVE: the escapes that would make the policy decorative.
test('script execution stays locked to same-origin files plus wasm', () => {
  for (const escape of ["'unsafe-eval'", "'unsafe-inline'", '*', 'data:', 'https:', 'http:']) {
    assert.ok(!directives['script-src'].includes(escape), `script-src must not grant ${escape}`);
  }
  assert.ok(!directives['default-src'].includes('*'));
  assert.ok(!directives['object-src'].includes("'self'"));
});

// frame-ancestors is deliberately absent: the extension frames this app in its in-page
// editor modal (browser-extension/src/lib/overlay.js), on an origin nobody can know up front.
test('nothing restricts frame-ancestors', () => {
  assert.equal(directives['frame-ancestors'], undefined);
});

test('nginx serves the SAME policy, on every location', () => {
  assert.ok(NGINX.includes(`default "${POLICY}";`), 'nginx.conf policy drifted from index.html');
  // add_header is NOT inherited into a location that sets one of its own, so /sw.js re-adds it.
  const added = NGINX.split('add_header Content-Security-Policy $stencil_csp always;').length - 1;
  assert.equal(added, 2);
});

test('the single-file build drops the meta — one all-inline file cannot obey it', () => {
  assert.ok(HTML.match(CSP_META));
  assert.ok(!HTML.replace(CSP_META, '').includes('Content-Security-Policy'));
});
