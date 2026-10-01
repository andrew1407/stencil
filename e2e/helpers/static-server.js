// Minimal static file server for the E2E harness, zero non-Node deps — Playwright's
// `webServer` launches it. It serves the checkout's browser/ (the app under test, at `/` on PORT,
// default 8188), common/ at /common/, and `/__e2e__/...` from ./fixtures, the host pages
// the scanner needs on an http origin. Not general-purpose: no listing, traversal blocked.
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { APP_PORT, APP_HOST } from './config.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '../..');
const FIXTURES_ROOT = path.resolve(HERE, '../fixtures');
// The only checkout trees a page may reach, by URL prefix; the app's ../common imports land on /common/.
const TREES = Object.freeze([
  ['/__e2e__', FIXTURES_ROOT],
  ['/common', path.join(REPO_ROOT, 'common')],
  ['', path.join(REPO_ROOT, 'browser')],
]);
const PORT = Number(process.env.PORT) || APP_PORT;
const HOST = process.env.ADDR || APP_HOST;

// ES modules refuse to load without the right Content-Type, so this table is the
// whole point of the server.
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webmanifest': 'application/manifest+json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.ico': 'image/x-icon',
  '.wasm': 'application/wasm',
  '.map': 'application/json',
};

const inside = (abs, root) => abs === root || abs.startsWith(root + path.sep);

// Resolve a URL path to an on-disk file under the fixtures or a served tree, refusing any
// path that escapes them.
const resolveFile = (urlPath) => {
  const rel = path.posix.normalize(decodeURIComponent(urlPath.split('?')[0]));
  const [prefix, root] = TREES.find(([p]) => rel === p || rel.startsWith(`${p}/`));
  let abs = path.join(root, rel.slice(prefix.length));
  if (!inside(abs, root)) return null;
  if (rel.endsWith('/')) abs = path.join(abs, 'index.html');
  return abs;
};

const server = http.createServer(async (req, res) => {
  const file = resolveFile(req.url || '/');
  if (!file) { res.writeHead(403).end('forbidden'); return; }
  try {
    const body = await readFile(file);
    const type = MIME[path.extname(file).toLowerCase()] || 'application/octet-stream';
    // Never let the SW / module cache leak state between test runs.
    res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    res.end(body);
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/plain' }).end('not found');
  }
});

server.listen(PORT, HOST, () => {
  // eslint-disable-next-line no-console
  console.log(`[e2e static] serving browser/ + common/ + fixtures on http://${HOST}:${PORT}/`);
});
