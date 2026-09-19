// The two static servers a capture needs: the browser app on e2e's harness port, and the
// demo "site" the extension is photographed on, served read-only out of the repo.
import { spawn } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { REPO } from './paths.mjs';
import { e2e } from './playwright.mjs';

const MIME = Object.freeze({
  '.html': 'text/html; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png',
  '.jpg': 'image/jpeg', '.css': 'text/css',
});

// An already-running app server is reused only when it serves THIS checkout (a sibling
// worktree's server looks identical otherwise).
export async function startAppServer() {
  const { APP_URL } = await e2e('helpers/config.js');
  const marker = fs.readFileSync(path.join(REPO, 'browser', 'js', 'index.js'), 'utf8');
  const servesThisTree = async () => {
    try { return (await (await fetch(`${APP_URL}js/index.js`)).text()) === marker; } catch { return false; }
  };
  if (await servesThisTree()) return { url: APP_URL, stop: () => {} };
  const child = spawn(process.execPath, [path.join(REPO, 'e2e', 'helpers', 'static-server.js')], { stdio: 'ignore' });
  for (let i = 0; i < 50; i++) {
    if (await servesThisTree()) return { url: APP_URL, stop: () => child.kill() };
    await delay(100);
  }
  child.kill();
  throw new Error(`the static server never answered on ${APP_URL} (is the port held by another tree?)`);
}

// CORS is open because the app FETCHES the clip to scrub it locally (a tainted canvas cannot be
// read back), Range is answered for <video>, and the port is fixed (shared.json mediaPort).
export function startMediaServer(dir, port) {
  const server = http.createServer((req, res) => {
    const abs = path.join(dir, decodeURIComponent((req.url || '/').split('?')[0]));
    if (!abs.startsWith(dir + path.sep) || !fs.existsSync(abs)) { res.writeHead(404).end(); return; }
    const body = fs.readFileSync(abs);
    const head = { 'Content-Type': 'video/mp4', 'Access-Control-Allow-Origin': '*',
                   'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store' };
    // The desktop STREAMS a URL through the platform media stack, which probes with HEAD
    // before it will touch the body — answer it, or the clip never starts.
    if (req.method === 'HEAD') { res.writeHead(200, { ...head, 'Content-Length': body.length }).end(); return; }
    const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
    if (!range) { res.writeHead(200, { ...head, 'Content-Length': body.length }).end(body); return; }
    const start = range[1] ? Number(range[1]) : 0;
    const end = range[2] ? Number(range[2]) : body.length - 1;
    res.writeHead(206, { ...head, 'Content-Length': end - start + 1,
                         'Content-Range': `bytes ${start}-${end}/${body.length}` });
    res.end(body.subarray(start, end + 1));
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1',
    () => resolve({ url: (name) => `http://127.0.0.1:${port}/${name}`, stop: () => server.close() })));
}

export const siteUrl = (port) => `http://127.0.0.1:${port}/usecases/capture-runner/site/index.html`;

export function startSiteServer(port) {
  const server = http.createServer((req, res) => {
    const rel = decodeURIComponent((req.url || '/').split('?')[0]);
    const abs = path.join(REPO, rel);
    const type = MIME[path.extname(abs).toLowerCase()];
    if (!abs.startsWith(REPO + path.sep) || !type || !fs.existsSync(abs)) { res.writeHead(404).end(); return; }
    res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    res.end(fs.readFileSync(abs));
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1',
    () => resolve({ url: siteUrl(port), stop: () => server.close() })));
}
