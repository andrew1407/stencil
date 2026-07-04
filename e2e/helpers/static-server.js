// Minimal static file server for the E2E harness — zero non-Node runtime deps
// (the browser app is dependency-free ES modules; the only requirement is correct
// MIME types so the module graph loads). Playwright's `webServer` launches this.
//
//   /            -> ../browser        (the app under test, served on PORT, default 8188 — see config.js)
//   /__e2e__/... -> ./fixtures        (host pages the extension scanner needs on an http origin)
//
// Not a general-purpose server: no directory listing, path-traversal is blocked.
import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { APP_PORT, APP_HOST } from './config.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const BROWSER_ROOT = path.resolve(HERE, '../../browser');
const FIXTURES_ROOT = path.resolve(HERE, '../fixtures');
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

// Resolve a URL path to an on-disk file under one of the two roots, refusing any
// path that escapes its root.
const resolveFile = (urlPath) => {
  let root = BROWSER_ROOT;
  let rel = decodeURIComponent(urlPath.split('?')[0]);
  if (rel.startsWith('/__e2e__/')) {
    root = FIXTURES_ROOT;
    rel = rel.slice('/__e2e__'.length);
  }
  if (rel === '/' || rel === '') rel = '/index.html';
  const abs = path.join(root, rel);
  if (abs !== root && !abs.startsWith(root + path.sep)) return null; // traversal guard
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
  console.log(`[e2e static] serving browser/ + fixtures on http://${HOST}:${PORT}`);
});
