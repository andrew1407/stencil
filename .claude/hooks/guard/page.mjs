// Page rules for the guard: what an evaluate_script may send off-origin, and which local files
// upload_file may put into a page.

import { isSecretPath, resolveAbs } from './paths.mjs';
import { isLocalHost } from './urls.mjs';
import { allow, ask, deny } from './verdict.mjs';

export function evaluateScriptDecision(input, ctx) {
  const src = JSON.stringify(input || {});
  const netCall = /\b(fetch|XMLHttpRequest|sendBeacon|WebSocket|EventSource)\b/.test(src) || /\.open\s*\(/.test(src);
  const urls = [...src.matchAll(/(?:https?|wss?):\/\/([^/\s"'`)\\]+)/gi)].map((m) => m[1]);
  const external = urls.some((h) => !isLocalHost(h, ctx));
  const secretSink = /document\.cookie|localStorage|sessionStorage|indexedDB|["'`]?authorization["'`]?|\.token\b/i.test(src);
  if (external && secretSink) {
    return deny('reads browser storage/cookies and sends them to a non-local origin');
  }
  if (netCall && external) return ask('makes an outbound request to a non-local origin');
  return allow();
}

export function uploadFileDecision(input, ctx) {
  for (const v of Object.values(input || {})) {
    if (typeof v === 'string' && isSecretPath(resolveAbs(v, ctx))) {
      return deny('uploads a secret/credential file into a web page');
    }
  }
  return ask('uploads a local file into the page');
}
