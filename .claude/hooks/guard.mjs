#!/usr/bin/env node
// PreToolUse guard entry (wired from .claude/settings.json): reads the tool call as JSON on
// stdin and prints ask / deny, or exits quietly on allow. The rules live in guard/ and load
// inside a try: a module that fails to load is an ask, never an allow — fail-CLOSED.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const ask = (reason) => ({ decision: 'ask', reason });

let rules = null;
let loadError = null;
try {
  rules = await import(new URL('./guard/decide.mjs', import.meta.url));
} catch (err) {
  loadError = err;
}

export function defaultCtx() {
  if (!rules) throw loadError;
  return rules.defaultCtx();
}

export function decide(payload, ctx) {
  if (!rules) throw loadError;
  return rules.decide(payload, ctx);
}

export function safeDecide(payload, ctx) {
  if (!rules) {
    return ask(`guard hook could not load its rules (failing closed): ${loadError && loadError.message ? loadError.message : loadError}`);
  }
  return rules.safeDecide(payload, ctx);
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

// Real paths on both sides: node resolves the main module's symlinks but argv[1] keeps them, so
// a repo reached through a symlink would otherwise skip main() — a silent allow.
const realOf = (p) => { try { return fs.realpathSync(p); } catch { return path.resolve(p); } };
const invokedDirectly = process.argv[1] && realOf(fileURLToPath(import.meta.url)) === realOf(process.argv[1]);
if (invokedDirectly) main();
