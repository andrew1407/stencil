// Bash rules for the guard: hard denies for destructive, exfiltrating or secret-reading
// commands, soft asks for the medium-risk ones, allow for the rest.

import fs from 'node:fs';
import { isOutsideRepo, isTmp, resolveAbs } from './paths.mjs';
import { allow, ask, deny } from './verdict.mjs';

const NETWORK_SINK = /\b(curl|wget|nc|ncat|netcat|scp|rsync|sftp|ftp|telnet|sendmail|Invoke-WebRequest|Invoke-RestMethod)\b/i;
// Consulted only together with commandTouchesSecret, so `grep foo src/` stays allowed.
// `(?<!\.)env` keeps the bare `env` command from matching the ".env" token.
const SECRET_READERS = /\b(cat|bat|head|tail|less|more|strings|xxd|od|hexdump|base64|type|Get-Content|gc|grep|egrep|fgrep|rg|sed|awk|perl|python3?|node|jq|sort|cp|mv|printenv|(?<!\.)env)\b/i;

// Secret path tokens, matched inside one shell word at a time.
const SECRET_TOKENS = [
  /(^|\/)\.env(\.(?!(example|sample|template|dist)$)[\w.-]+)?$/gi, // .env, .env.local — not .env.example
  /\bid_(rsa|ed25519|dsa|ecdsa)\b/g,
  /\.(pem|key|p12|pfx|jks|keystore)$/gi,
  /(^|\/)\.(ssh|aws|gnupg)\//g,
  /\bopenInConfig\.json\b/g,
  /\.git\/config$/g,
  // credentials files only — NOT the bare word, so `grep -r credentials src/` stays allowed
  /\bcredentials\.json\b/gi, /\.git-credentials\b/gi, /\/credentials$/gi, /\/secrets\.json$/g,
];
const WORD_SPLIT = /[\s'"`;|&()<>=,{}[\]]+/;

// A token counts only in a path-shaped word: one holding a `/`, one the token starts, or one
// naming a file that exists — so `SHELL.env` or `variable.other.key` in a one-liner do not.
function commandTouchesSecret(cmd, ctx) {
  if (/\bgcloud\b/.test(cmd)) return true;
  const exists = ctx.exists || fs.existsSync;
  return cmd.split(WORD_SPLIT).some((raw) => {
    const word = raw.replace(/^@/, '');
    return SECRET_TOKENS.some((re) => [...word.matchAll(re)].some((m) =>
      word.includes('/') || m.index === 0 || exists(resolveAbs(word, ctx))));
  });
}

export function bashDecision(cmd, ctx) {
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
  if (commandTouchesSecret(c, ctx)) {
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
  if (/\bstencil\b[^\n;|&]*\s--remote-update\b/.test(c)) {
    return ask('--remote-update overwrites a shared server project');
  }
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
