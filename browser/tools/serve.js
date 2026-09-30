#!/usr/bin/env node
// `npm run serve`: node's built-in http plus Cache-Control: no-store, so a plain refresh always picks up edited
// JS/CSS — a caching server lets Chrome heuristically cache module files. It serves browser/ at `/` and common/
// at /common/, where the app's ../common imports land (a URL's `..` stops at the root).
import { createReadStream } from 'node:fs';
import { readdir, stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { dirname, extname, join, normalize, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
// The only trees a page may reach; the rest of the checkout (sources, .git, env files) stays unserved.
const APP = join(repo, 'browser');
const COMMON = join(repo, 'common');

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webmanifest': 'application/manifest+json',
  '.map': 'application/json; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.mp4': 'video/mp4',
  '.webm': 'video/webm',
  '.wasm': 'application/wasm',
  '.woff2': 'font/woff2',
};

const inside = (target, tree) => target === tree || target.startsWith(tree + sep);

function resolveTarget(url) {
  const path = normalize(decodeURIComponent(new URL(url, 'http://x').pathname));
  const common = path === '/common' || path.startsWith('/common/');
  const tree = common ? COMMON : APP;
  const target = join(tree, common ? path.slice('/common'.length) : path);
  return inside(target, tree) ? target : null;
}

function send(res, status, body, type = TYPES['.txt']) {
  res.writeHead(status, { 'Cache-Control': 'no-store', 'Content-Type': type });
  res.end(body);
}

async function listing(dir, path) {
  const rows = (await readdir(dir, { withFileTypes: true }))
    .map(e => (e.isDirectory() ? `${e.name}/` : e.name))
    .sort()
    .map(n => `<li><a href="${encodeURIComponent(n).replace(/%2F$/, '/')}">${n}</a></li>`);
  return `<!doctype html><meta charset="utf-8"><title>${path}</title><h1>${path}</h1><ul>${rows.join('')}</ul>`;
}

const server = createServer(async (req, res) => {
  if (req.method !== 'GET' && req.method !== 'HEAD') return send(res, 405, 'Method Not Allowed');
  const { pathname: asked, search: query } = new URL(req.url, 'http://x');
  // An old /browser/ link moves to the root; the browser carries its fragment across the redirect.
  if (asked === '/browser' || asked.startsWith('/browser/')) {
    res.writeHead(302, { 'Cache-Control': 'no-store', Location: `${asked.slice('/browser'.length) || '/'}${query}` });
    return res.end();
  }
  let file = resolveTarget(req.url);
  if (!file) return send(res, 403, 'Forbidden');
  try {
    let info = await stat(file);
    if (info.isDirectory()) {
      const { pathname, search } = new URL(req.url, 'http://x');
      if (!pathname.endsWith('/')) {
        res.writeHead(301, { 'Cache-Control': 'no-store', Location: `${pathname}/${search}` });
        return res.end();
      }
      const index = join(file, 'index.html');
      const found = await stat(index).catch(() => null);
      if (!found) return send(res, 200, req.method === 'HEAD' ? '' : await listing(file, pathname), TYPES['.html']);
      file = index;
      info = found;
    }
    res.writeHead(200, {
      'Cache-Control': 'no-store',
      'Content-Type': TYPES[extname(file).toLowerCase()] || 'application/octet-stream',
      'Content-Length': info.size,
    });
    if (req.method === 'HEAD') return res.end();
    createReadStream(file).pipe(res);
  } catch {
    send(res, 404, 'Not Found');
  }
});

const addr = process.env.ADDR || 'localhost';
const port = Number(process.env.PORT || 8080);
server.listen(port, addr, () => {
  console.log(`Serving on http://${addr}:${port}/ (Cache-Control: no-store)`);
});
process.on('SIGINT', () => process.exit(0));
