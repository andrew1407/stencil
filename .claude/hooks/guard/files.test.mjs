// Unit tests for the guard's file and search rules. Run: node --test .claude/hooks/guard/*.test.mjs
// Pure logic — no process spawning, no real filesystem writes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decide } from '../guard.mjs';
import { ctx } from './testCtx.mjs';

const read = (file_path) => decide({ tool_name: 'Read', tool_input: { file_path } }, ctx);
const write = (file_path) => decide({ tool_name: 'Write', tool_input: { file_path } }, ctx);
const grep = (input) => decide({ tool_name: 'Grep', tool_input: input }, ctx);
const glob = (input) => decide({ tool_name: 'Glob', tool_input: input }, ctx);

test('secret files are denied for Read/Write; templates allowed', () => {
  assert.equal(read('/repo/server/.env').decision, 'deny');
  assert.equal(read('/repo/bot/.env').decision, 'deny');
  assert.equal(read('/repo/browser/js/config/openInConfig.json').decision, 'deny');
  assert.equal(read('/home/user/.ssh/id_rsa').decision, 'deny');
  assert.equal(read('/repo/certs/server.pem').decision, 'deny');
  assert.equal(read('/repo/server/.env.example').decision, 'allow');
  assert.equal(read('/repo/browser/js/index.js').decision, 'allow');
});

test('every .env variant and the desktop token store are secrets', () => {
  assert.equal(read('/repo/server/.env.local').decision, 'deny');
  assert.equal(read('/repo/e2e/.env.production').decision, 'deny');
  assert.equal(read('/repo/.env.development.local').decision, 'deny');
  assert.equal(read('/home/user/Library/Application Support/stencil/secrets.json').decision, 'deny');
  assert.equal(read('/repo/mcp/.env.sample').decision, 'allow');
  assert.equal(read('/repo/docs/.environment.md').decision, 'allow');
  assert.equal(glob({ pattern: '**/.env.local' }).decision, 'deny');
});

test('writes outside the repo soft-ask; secret writes deny', () => {
  assert.equal(write('/etc/hosts').decision, 'ask');
  assert.equal(write('/repo/out/result.png').decision, 'allow');
  assert.equal(write('/repo/bot/.env').decision, 'deny');
});

test('Grep/Glob: a secret path, glob or pattern denies; ordinary searches pass', () => {
  assert.equal(grep({ pattern: 'API', path: '/repo/server/.env' }).decision, 'deny');
  assert.equal(grep({ pattern: 'API', path: '/repo', glob: '**/.env' }).decision, 'deny');
  assert.equal(grep({ pattern: 'BEGIN', glob: '*.{pem,key}' }).decision, 'deny');
  assert.equal(grep({ pattern: 'x', path: '~/.ssh' }).decision, 'deny');
  assert.equal(glob({ pattern: '**/.env*' }).decision, 'deny');
  assert.equal(glob({ pattern: '**/id_rsa' }).decision, 'deny');
  assert.equal(glob({ pattern: '*', path: '/home/user/.aws' }).decision, 'deny');
  // the Grep *pattern* is content, not a path: searching code for the word is fine
  assert.equal(grep({ pattern: 'id_rsa|\\.env', path: 'browser/js' }).decision, 'allow');
  assert.equal(grep({ pattern: 'TODO', glob: '*.js' }).decision, 'allow');
  assert.equal(glob({ pattern: '**/*.js' }).decision, 'allow');
  assert.equal(glob({ pattern: '**/.env.example' }).decision, 'allow');
});
