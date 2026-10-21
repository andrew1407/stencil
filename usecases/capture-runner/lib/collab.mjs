// The collaboration server the server-side shots connect to: the one config.serverUrl names
// when it answers, else a throwaway one built from this checkout — a fresh Postgres cluster
// under the scratch dir, the in-process event bus and a per-run admin token, all on loopback.
import { execFileSync, spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { repoPath, scratchPath } from './paths.mjs';

const answers = async (url) => {
  try { return (await fetch(`${url}/healthz`)).ok; } catch { return false; }
};

const quiet = (cmd, args, opts = {}) => execFileSync(cmd, args, { stdio: 'ignore', ...opts });

// A session token from the admin gate: one per client, so a co-edit shows two people.
const minter = (url, admin) => async () => {
  const res = await fetch(`${url}/auth/token`, { method: 'POST', headers: { 'X-Admin-Token': admin } });
  if (!res.ok) throw new Error(`token issuance answered ${res.status}`);
  return (await res.json()).token;
};

const startDatabase = (dir, port) => {
  const data = path.join(dir, 'pgdata');
  fs.rmSync(data, { recursive: true, force: true });
  quiet('initdb', ['-D', data, '-U', 'stencil', '--auth=trust', '-E', 'UTF8']);
  quiet('pg_ctl', ['-D', data, '-l', path.join(dir, 'postgres.log'), '-w', 'start', '-o',
    `-p ${port} -c listen_addresses=127.0.0.1 -c unix_socket_directories=''`]);
  return () => quiet('pg_ctl', ['-D', data, '-m', 'fast', '-w', 'stop']);
};

const buildServer = (dir) => {
  const binary = path.join(dir, 'stencil-server');
  quiet('go', ['generate', './...'], { cwd: repoPath('server') });
  quiet('go', ['build', '-o', binary, './cmd/stencil-server'], { cwd: repoPath('server') });
  return binary;
};

// One scratch dir per server port; a running local server leaves its admin token there, so a
// second capture against the same port can mint its own.
const dirFor = (url) => scratchPath(`collab-${new URL(url).port}`);
const adminFile = (url) => path.join(dirFor(url), 'admin');
const readAdmin = (url) => {
  try { return fs.readFileSync(adminFile(url), 'utf8').trim() || null; } catch { return null; }
};

// Resolves { url, local, mint(), stop() }; `mint` is null when an outside server gave no token.
export async function startCollab(config) {
  const url = config.serverUrl.replace(/\/$/, '');
  if (await answers(url)) {
    const admin = process.env.STENCIL_DOCS_ADMIN_TOKEN || readAdmin(url);
    const token = config.serverToken();
    const mint = admin ? minter(url, admin) : token ? async () => token : null;
    return { url, local: false, mint, stop: async () => {} };
  }
  const dir = dirFor(url);
  fs.mkdirSync(dir, { recursive: true });
  const { port, hostname } = new URL(url);
  const dbPort = config.get('server.localDbPort');
  const stopDatabase = startDatabase(dir, dbPort);
  const admin = crypto.randomBytes(24).toString('hex');
  const child = spawn(buildServer(dir), [], {
    stdio: ['ignore', 'ignore', fs.openSync(path.join(dir, 'server.log'), 'w')],
    env: {
      ...process.env,
      LISTEN_ADDR: `${hostname}:${port}`,
      TCP_ADDR: `${hostname}:${Number(port) + 1}`,
      DATABASE_URL: `postgres://stencil@127.0.0.1:${dbPort}/postgres?sslmode=disable`,
      REDIS_URL: '',
      FILESTORE_ROOT: path.join(dir, 'files'),
      ADMIN_TOKEN: admin,
      AUTH_RATE_PER_MINUTE: '0',
    },
  });
  let stopped = false;
  const stopNow = () => {
    if (stopped) return;
    stopped = true;
    fs.rmSync(adminFile(url), { force: true });
    child.kill();
    stopDatabase();
  };
  const stop = async () => stopNow();
  // A capture that throws never reaches its own stop(): the server goes down with the process.
  process.once('exit', stopNow);
  for (let i = 0; i < 120 && !(await answers(url)); i++) await delay(250);
  if (!(await answers(url))) {
    await stop();
    throw new Error(`the local collaboration server never answered on ${url} (see ${dir}/server.log)`);
  }
  fs.writeFileSync(adminFile(url), admin, { mode: 0o600 });
  return { url, local: true, mint: minter(url, admin), stop };
}
