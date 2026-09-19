#!/usr/bin/env node
// PreToolUse guard (wired from .claude/settings.json): decide() reads the tool call as JSON on
// stdin and prints allow / ask / deny. Runs in EVERY permission mode; fail-CLOSED — errors ask.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const DEFAULT_LOCAL_ORIGINS = ['localhost', '127.0.0.1', '0.0.0.0', '::1', 'host.docker.internal'];


export function defaultCtx() {
  const repoRoot = process.env.CLAUDE_PROJECT_DIR || process.cwd();
  let allowedOrigins = DEFAULT_LOCAL_ORIGINS;
  try {
    const raw = fs.readFileSync(path.join(repoRoot, '.claude', 'hooks', 'allowed-origins.json'), 'utf8');
    const extra = JSON.parse(raw);
    if (Array.isArray(extra)) allowedOrigins = [...new Set([...DEFAULT_LOCAL_ORIGINS, ...extra.map(String)])];
  } catch {
    /* no override file — defaults are fine */
  }
  return { repoRoot, homeDir: os.homedir(), allowedOrigins };
}

const allow = () => ({ decision: 'allow' });
const ask = (reason) => ({ decision: 'ask', reason });
const deny = (reason) => ({ decision: 'deny', reason });


function toPosix(p) {
  return p.split(path.sep).join('/').replace(/\\/g, '/');
}

function resolveAbs(p, ctx) {
  let s = String(p);
  if (s === '~' || s.startsWith('~/') || s.startsWith('~\\')) s = ctx.homeDir + s.slice(1);
  if (!path.isAbsolute(s)) s = path.resolve(ctx.repoRoot, s);
  return path.normalize(s);
}

function isTmp(abs) {
  const p = toPosix(abs).toLowerCase();
  return p.startsWith('/tmp/') || p.startsWith('/private/tmp/') || p.startsWith('/var/folders/') ||
    p.startsWith(toPosix(os.tmpdir()).toLowerCase());
}

function isOutsideRepo(p, ctx) {
  const abs = resolveAbs(p, ctx);
  const rel = path.relative(ctx.repoRoot, abs);
  return rel === '' ? false : (rel.startsWith('..') || path.isAbsolute(rel));
}

// A file whose contents are secret/credential material. `.env.example` and friends
// are explicitly NOT secret (they are committed templates).
function isSecretPath(abs) {
  const posix = toPosix(abs);
  const base = posix.split('/').pop() || '';
  if (/^\.env\.(example|sample|template|dist)$/i.test(base)) return false;
  if (base.toLowerCase() === '.env') return true;
  if (/\.(pem|key|p12|pfx|jks|keystore)$/i.test(base)) return true;
  if (base === 'id_rsa' || base === 'id_ed25519' || base === 'id_dsa' || base === 'id_ecdsa') return true;
  if (base === 'openInConfig.json') return true;
  if (/\/\.ssh\//.test(posix) || /\/\.aws\//.test(posix) || /\/\.config\/gcloud\//.test(posix) ||
      /\/\.gnupg\//.test(posix) || /\/\.docker\/config\.json$/.test(posix)) return true;
  if (/\/\.git\/config$/.test(posix)) return true;
  return false;
}


const NETWORK_SINK = /\b(curl|wget|nc|ncat|netcat|scp|rsync|sftp|ftp|telnet|sendmail|Invoke-WebRequest|Invoke-RestMethod)\b/i;
// Consulted only together with commandTouchesSecret, so `grep foo src/` stays allowed.
// `(?<!\.)env` keeps the bare `env` command from matching the ".env" token.
const SECRET_READERS = /\b(cat|bat|head|tail|less|more|strings|xxd|od|hexdump|base64|type|Get-Content|gc|grep|egrep|fgrep|rg|sed|awk|perl|python3?|node|jq|sort|cp|mv|printenv|(?<!\.)env)\b/i;

// A secret path *token* referenced inside a raw command string.
function commandTouchesSecret(cmd) {
  return (
    /\.env(?![.\w])/.test(cmd) ||               // .env  (but NOT .env.example / .environment)
    /\bid_rsa\b|\bid_ed25519\b|\bid_dsa\b|\bid_ecdsa\b/.test(cmd) ||
    /\.(pem|key|p12|pfx|jks|keystore)(["'\s]|$)/i.test(cmd) ||
    /(^|[\s"'`/=])\.ssh\//.test(cmd) || /(^|[\s"'`/=])\.aws\//.test(cmd) ||
    /(^|[\s"'`/=])\.gnupg\//.test(cmd) || /\bgcloud\b/.test(cmd) ||
    /\bopenInConfig\.json\b/.test(cmd) ||
    /\.git\/config(["'\s]|$)/.test(cmd) ||
    // path-shaped credentials files only (~/.aws/credentials, credentials.json,
    // .git-credentials) — NOT the bare word, so `grep -r credentials src/` stays allowed
    /\bcredentials\.json\b/i.test(cmd) || /\.git-credentials\b/i.test(cmd) || /\/credentials(["'\s]|$)/i.test(cmd)
  );
}

function bashDecision(cmd, ctx) {
  const c = String(cmd);

  // ---- hard denies: irreversible / destructive / exfiltration ----
  if (/:\s*\(\s*\)\s*\{\s*:\s*\|\s*:\s*&\s*\}\s*;\s*:/.test(c.replace(/\s+/g, ' '))) {
    return deny('fork bomb');
  }
  if (/\bdd\b[^\n]*\bof=\s*\/dev\//.test(c) || /\bmkfs(\.\w+)?\b/.test(c) || />\s*\/dev\/(sd|nvme|disk|hd|mmcblk)/.test(c)) {
    return deny('raw write to a disk device');
  }
  if (/\b(curl|wget|fetch)\b[^|]*\|\s*(sudo\s+)?(sh|bash|zsh|dash|ksh|fish|python3?|node|ruby|perl|pwsh|powershell)\b/i.test(c)) {
    return deny('pipes network content straight into a shell/interpreter');
  }
  if (/\bRemove-Item\b[^\n]*-Recurse/i.test(c) || /\brmdir\b[^\n]*\/s\b/i.test(c) ||
      /\bdel\b[^\n]*\/s\b/i.test(c) || /\bformat\b\s+[a-z]:/i.test(c)) {
    return deny('recursive Windows delete / drive format');
  }
  // rm targeting a root/home path
  const rm = c.match(/\brm\b([^\n;|&]*)/);
  if (rm) {
    const args = rm[1];
    const recursive = /(^|\s)-[a-z]*r/i.test(args) || /--recursive/i.test(args);
    const catastrophic = /(^|\s)(\/|\/\*|~|~\/|\$HOME|\$\{HOME\}|\.\.)(\s|$)/.test(args) || /--no-preserve-root/.test(args);
    if (catastrophic) return deny('recursive delete of a root/home path');
    if (recursive) return ask('recursive force-delete (rm -r)');
  }
  // secret handling
  if (commandTouchesSecret(c)) {
    if (SECRET_READERS.test(c)) return deny('reads or copies a secret/credential file');
    if (NETWORK_SINK.test(c)) return deny('sends a secret/credential over the network');
  }

  // ---- soft asks: medium-risk, confirm case-by-case ----
  if (/\bsudo\b/.test(c)) return ask('runs with sudo');
  if (/\b(brew|apt|apt-get|dnf|yum|pacman|npm|pnpm|yarn|pip|pip3|cargo|go|dotnet|gem|choco|winget)\b[^\n]*\b(install|add)\b/i.test(c) ||
      /\bnpm\s+i\b/.test(c)) return ask('installs packages');
  // force-push first, so the prompt names the history rewrite rather than a plain push
  if (/\bgit\s+push\b[^\n;|&]*(--force-with-lease|--force\b|\s-f\b)/.test(c)) {
    return ask('git push --force rewrites remote history');
  }
  if (/\bgit\s+push\b/.test(c)) return ask('git push');
  if (/\bgit\s+reset\s+--hard\b/.test(c)) return ask('git reset --hard discards changes');
  if (/\bgit\s+clean\s+-[a-z]*f/i.test(c)) return ask('git clean -f deletes untracked files');
  if (/\bgit\s+checkout\s+(--|\.)/.test(c)) return ask('git checkout discards local changes');
  // `-D` only: `git branch -d` refuses to drop unmerged work, `-D` does not
  if (/\bgit\s+branch\b[^\n;|&]*\s-D\b/.test(c)) return ask('git branch -D deletes an unmerged branch');
  if (/\bgit\s+stash\s+(drop|clear)\b/.test(c)) return ask('git stash drop/clear discards stashed work');
  // `docker compose down -v` removes the named volumes — i.e. the local Postgres the
  // server/e2e stack keeps its data in. Plain `down` (no -v) stays allowed.
  if (/\bdocker(-compose\b|\s+compose\b)[^\n;|&]*\bdown\b[^\n;|&]*(\s-[a-zA-Z]*v\b|--volumes\b)/.test(c) ||
      /\bdocker\s+volume\s+rm\b/.test(c)) {
    return ask("destroys a docker volume (the local database's data)");
  }
  if (/\b(chmod|chown)\b/.test(c)) return ask('changes file ownership/permissions');
  if (/\bkill\s+-9\b/.test(c) || /\b(pkill|killall)\b/.test(c)) return ask('force-kills processes');
  if (/\b(crontab|launchctl|schtasks|systemctl)\b/.test(c)) return ask('schedules/daemonizes a process');
  // redirected write to a path outside the repo (and not a device/tmp)
  const redir = c.match(/>>?\s*("?)([^\s"'|;&<>]+)\1/);
  if (redir) {
    const target = redir[2];
    if (!/^\/dev\/(null|stdout|stderr|tty)$/.test(target) && isOutsideRepo(target, ctx) && !isTmp(resolveAbs(target, ctx))) {
      return ask('redirects output to a path outside the repository');
    }
  }
  return allow();
}


function fileDecision(toolName, input, ctx) {
  const fp = input.file_path || input.path || input.notebook_path;
  if (!fp) return allow();
  const abs = resolveAbs(fp, ctx);
  if (isSecretPath(abs)) return deny('accesses a secret/credential file');
  if (toolName !== 'Read') {
    if (isOutsideRepo(fp, ctx) && !isTmp(abs)) return ask('writes to a path outside the repository');
  }
  return allow();
}


function isLocalHost(hostport, ctx) {
  let h = String(hostport).trim().replace(/^\[/, '').replace(/\].*$/, '').replace(/:\d+$/, '').toLowerCase();
  if (!h) return true; // relative URL — same origin
  if (/^127\./.test(h) || h === '::1' || h === '::' || /^0\.0\.0\.0$/.test(h)) return true;
  return ctx.allowedOrigins.map((o) => String(o).toLowerCase()).includes(h);
}

function evaluateScriptDecision(input, ctx) {
  const src = JSON.stringify(input || {});
  const netCall = /\b(fetch|XMLHttpRequest|sendBeacon|WebSocket|EventSource)\b/.test(src) || /\.open\s*\(/.test(src);
  const urls = [...src.matchAll(/(?:https?|wss?):\/\/([^/\s"'`)\\]+)/gi)].map((m) => m[1]);
  const external = urls.some((h) => !isLocalHost(h, ctx));
  const secretSink = /document\.cookie|localStorage|sessionStorage|indexedDB|["'`]?authorization["'`]?|\.token\b/i.test(src);
  if ((netCall || external) && external && secretSink) {
    return deny('reads browser storage/cookies and sends them to a non-local origin');
  }
  if (netCall && external) return ask('makes an outbound request to a non-local origin');
  return allow();
}

function uploadFileDecision(input, ctx) {
  for (const v of Object.values(input || {})) {
    if (typeof v === 'string' && isSecretPath(resolveAbs(v, ctx))) {
      return deny('uploads a secret/credential file into a web page');
    }
  }
  return ask('uploads a local file into the page');
}


// Secret tokens for URL/query text — deliberately tighter than commandTouchesSecret:
// no bare `credentials`/`gcloud` words, so ordinary doc lookups don't trip it.
const URL_SECRET_TOKEN = /\.env(?![.\w])|\bid_rsa\b|\bid_ed25519\b|\bid_dsa\b|\bid_ecdsa\b|\.ssh\//i;

const BASE64ISH_BLOB = /[A-Za-z0-9+/=_-]{200,}/; // a long unbroken encodable run

// Returns a decision for a URL an outbound request will hit, or null for a normal page load.
// Local/user-allowlisted origins are always fine — `#stencil=` fragments are huge on localhost.
function urlExfilDecision(rawUrl, ctx) {
  const s = String(rawUrl || '');
  let u;
  try {
    u = new URL(s);
  } catch {
    return null; // relative or non-URL — nothing to judge here
  }
  if (isLocalHost(u.hostname, ctx)) return allow();
  if (URL_SECRET_TOKEN.test(s)) return deny('URL mentions a secret/credential path');
  if (/^\d{1,3}(\.\d{1,3}){3}$/.test(u.hostname) || u.hostname.startsWith('[')) {
    return ask('request to a raw IP address (not a named host)');
  }
  const tail = (u.search || '') + (u.hash || '');
  if (tail.length > 512 || BASE64ISH_BLOB.test(tail)) {
    return ask('exfil-shaped URL: very long or base64-looking query/fragment');
  }
  return null;
}

function webFetchDecision(input, ctx) {
  return urlExfilDecision(input && input.url, ctx) || allow();
}

function webSearchDecision(input) {
  const q = String((input && input.query) || '');
  if (URL_SECRET_TOKEN.test(q)) return ask('search query mentions a secret/credential path');
  if (BASE64ISH_BLOB.test(q) || q.length > 1000) {
    return ask('exfil-shaped search query: very long or base64-looking blob');
  }
  return allow();
}

function navigationDecision(input, ctx) {
  return urlExfilDecision(input && input.url, ctx) || allow();
}


export function decide(payload, ctx = defaultCtx()) {
  const tool = payload && payload.tool_name;
  const input = (payload && payload.tool_input) || {};
  switch (tool) {
    case 'Bash':
      return bashDecision(input.command || '', ctx);
    case 'Read':
    case 'Edit':
    case 'Write':
    case 'NotebookEdit':
      return fileDecision(tool, input, ctx);
    case 'mcp__chrome-devtools__evaluate_script':
      return evaluateScriptDecision(input, ctx);
    case 'mcp__chrome-devtools__upload_file':
      return uploadFileDecision(input, ctx);
    case 'mcp__chrome-devtools__navigate_page':
    case 'mcp__chrome-devtools__new_page':
      return navigationDecision(input, ctx);
    case 'WebFetch':
      return webFetchDecision(input, ctx);
    case 'WebSearch':
      return webSearchDecision(input);
    default:
      return allow(); // anything else — unrestricted
  }
}


function readStdin() {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (c) => (data += c));
    process.stdin.on('end', () => resolve(data));
    process.stdin.on('error', () => resolve(data));
  });
}

// Fail-closed wrapper: any exception inside decide() becomes an `ask` whose prompt
// explains why, so an internal guard bug can never turn into a silent allow.
export function safeDecide(payload, ctx) {
  try {
    return ctx === undefined ? decide(payload) : decide(payload, ctx);
  } catch (err) {
    return ask(`guard hook hit an internal error (failing closed): ${err && err.message ? err.message : err}`);
  }
}

async function main() {
  let result;
  let payload;
  try {
    payload = JSON.parse(await readStdin());
  } catch (err) {
    // fail-closed: an unparseable payload surfaces as an ask, never a silent allow
    result = ask(`guard hook could not parse the tool payload (failing closed): ${err && err.message ? err.message : err}`);
  }
  if (!result) result = safeDecide(payload);
  if (result.decision === 'allow') process.exit(0);
  process.stdout.write(JSON.stringify({
    hookSpecificOutput: {
      hookEventName: 'PreToolUse',
      permissionDecision: result.decision, // 'deny' | 'ask'
      permissionDecisionReason: `Stencil harness guard: ${result.reason}.`,
    },
  }));
  process.exit(0);
}

const invokedDirectly = process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1]);
if (invokedDirectly) main();
