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
// editor modal (src/lib/drop/overlay.js), on an origin nobody can know up front.
test('nothing restricts frame-ancestors', () => {
  assert.equal(directives['frame-ancestors'], undefined);
});

test('nginx serves the SAME policy, on every location', () => {
  assert.ok(NGINX.includes(`default "${POLICY}";`), 'nginx.conf policy drifted from index.html');
  // add_header is NOT inherited into a location that sets one of its own, so each such location
  // re-adds it: the server level once, then every location block with an add_header.
  const CSP = 'add_header Content-Security-Policy $stencil_csp always;';
  const blocks = [...NGINX.matchAll(/location\b[^{]*\{([^}]*)\}/g)].map((m) => m[1]);
  assert.ok(blocks.length >= 2, 'read the location blocks out of nginx.conf');
  for (const body of blocks.filter((b) => b.includes('add_header')))
    assert.ok(body.includes(CSP), `a location sets a header without the CSP:\n${body}`);
  const server = NGINX.replace(/location\b[^{]*\{[^}]*\}/g, '');
  assert.ok(server.includes(CSP), 'the server level carries the policy for every other location');
});

test('the single-file build drops the meta — one all-inline file cannot obey it', () => {
  assert.ok(HTML.match(CSP_META));
  assert.ok(!HTML.replace(CSP_META, '').includes('Content-Security-Policy'));
});

// mime.types carries its own `types {}` block: included inside ours, nginx refuses the whole config
// and the image never starts. It is included beside it.
test('nginx.conf never nests mime.types inside a types block', () => {
  assert.doesNotMatch(NGINX, /types\s*\{[^}]*include\s+\/etc\/nginx\/mime\.types/);
  assert.match(NGINX, /^\s*include \/etc\/nginx\/mime\.types;/m);
});

// Paths are stable across deploys, so a cached module outliving its index.html boots a mixed app:
// every file nginx sets a lifetime on revalidates instead.
test('nginx.conf caches no file past a revalidation', () => {
  const lifetimes = [...NGINX.matchAll(/add_header Cache-Control "([^"]*)"/g)].map((m) => m[1]);
  assert.ok(lifetimes.length >= 2, 'read the Cache-Control headers out of nginx.conf');
  for (const v of lifetimes) assert.equal(v, 'no-cache');
});
