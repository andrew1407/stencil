// Unit tests for the guard entry: dispatch, the fail-closed wrapper, and the process itself —
// a rules folder that is missing or broken must print an ask, never exit quietly as an allow.
// Run: node --test .claude/hooks/guard.test.mjs .claude/hooks/guard/*.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { decide, safeDecide } from './guard.mjs';
import { ctx } from './guard/testCtx.mjs';

const ENTRY = fileURLToPath(new URL('./guard.mjs', import.meta.url));

// Runs an entry script as the harness does and returns its printed decision, '' on a quiet allow.
function runEntry(entry, payload) {
  const r = spawnSync(process.execPath, [entry], { input: JSON.stringify(payload), encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  return r.stdout ? JSON.parse(r.stdout).hookSpecificOutput : '';
}

// A copy of the entry in a temp folder, beside a rules folder `layout` fills in; realpath, since
// the entry compares its own resolved URL with argv[1].
function strandedEntry(layout) {
  const dir = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'stencil_guard_')));
  fs.copyFileSync(ENTRY, path.join(dir, 'guard.mjs'));
  layout(path.join(dir, 'guard'));
  return path.join(dir, 'guard.mjs');
}

const SAFE = { tool_name: 'Bash', tool_input: { command: 'git status' } };

test('unknown tools are allowed', () => {
  assert.equal(decide({ tool_name: 'TodoWrite', tool_input: { todos: [] } }, ctx).decision, 'allow');
});

test('internal errors fail closed as ask, with the error in the reason', () => {
  const booby = new Proxy({}, { get() { throw new Error('boom from tool_input'); } });
  const r = safeDecide({ tool_name: 'Bash', tool_input: booby }, ctx);
  assert.equal(r.decision, 'ask');
  assert.match(r.reason, /failing closed/);
  assert.match(r.reason, /boom from tool_input/);
  // a healthy payload still flows through safeDecide unchanged
  assert.equal(safeDecide({ tool_name: 'Bash', tool_input: { command: 'git status' } }, ctx).decision, 'allow');
});

test('the live entry prints a deny, and stays quiet on an allow', () => {
  assert.equal(runEntry(ENTRY, SAFE), '');
  const out = runEntry(ENTRY, { tool_name: 'Bash', tool_input: { command: 'rm -rf ~' } });
  assert.equal(out.permissionDecision, 'deny');
});

test('an unparseable payload fails closed as ask', () => {
  const r = spawnSync(process.execPath, [ENTRY], { input: 'not json', encoding: 'utf8' });
  assert.equal(JSON.parse(r.stdout).hookSpecificOutput.permissionDecision, 'ask');
});

test('a missing or broken rules module fails closed as ask, even for a safe command', () => {
  const layouts = [
    () => {},
    (dir) => { fs.mkdirSync(dir); fs.writeFileSync(path.join(dir, 'decide.mjs'), 'export function decide( {'); },
    (dir) => { fs.mkdirSync(dir); fs.writeFileSync(path.join(dir, 'decide.mjs'), "import './gone.mjs';"); },
  ];
  for (const layout of layouts) {
    const entry = strandedEntry(layout);
    const out = runEntry(entry, SAFE);
    fs.rmSync(path.dirname(entry), { recursive: true, force: true });
    assert.equal(out.permissionDecision, 'ask');
    assert.match(out.permissionDecisionReason, /could not load its rules \(failing closed\)/);
  }
});

// A relative script path fails to load once the shell has left the repo root, and a hook
// that cannot start is a non-blocking error — the call would go through unguarded.
test('every hook in settings.json names its script from $CLAUDE_PROJECT_DIR', () => {
  const settings = JSON.parse(fs.readFileSync(new URL('../settings.json', import.meta.url), 'utf8'));
  const commands = Object.values(settings.hooks).flat().flatMap((m) => m.hooks.map((h) => h.command));
  assert.ok(commands.length >= 2);
  for (const command of commands) assert.match(command, /^node "\$CLAUDE_PROJECT_DIR\/\.claude\/[^"]+\.mjs"/);
});

test('the entry still decides when it is run through a symlink', () => {
  const dir = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'stencil_guard_')));
  const link = path.join(dir, 'guard.mjs');
  fs.symlinkSync(ENTRY, link);
  const out = runEntry(link, { tool_name: 'Bash', tool_input: { command: 'rm -rf ~' } });
  fs.rmSync(dir, { recursive: true, force: true });
  assert.equal(out.permissionDecision, 'deny');
});
