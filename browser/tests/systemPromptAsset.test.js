import { test } from 'node:test';
import assert from 'node:assert';
import PROMPT_ASSET from '../js/config/llm/systemPrompt.json' with { type: 'json' };
import {
  PROMPT_CORE_HEAD, PROMPT_CORE_TAIL, LLM_SYSTEM_PROMPT,
  OPS, BROWSER_CAPABILITIES,
} from '../js/llm/opPlan.js';

// ── The §4 prose core is a data asset (config/llm/systemPrompt.json) ──
// The JSON is the single source: opPlan.js re-exports its strings, no test or
// module carries a second literal of the prompt.

test('prompt asset: opPlan head/tail are the asset strings, byte-identical', () => {
  assert.strictEqual(PROMPT_CORE_HEAD, PROMPT_ASSET.head);
  assert.strictEqual(PROMPT_CORE_TAIL, PROMPT_ASSET.tail);
  // Exactly the four prose blocks — nothing else rides in the asset.
  assert.deepStrictEqual(Object.keys(PROMPT_ASSET).sort(), ['extensionHead', 'extensionTail', 'head', 'tail']);
  for (const v of Object.values(PROMPT_ASSET)) {
    assert.strictEqual(typeof v, 'string');
    assert.ok(!v.includes('﻿') && !v.includes('\r'));   // no BOM, LF-only
  }
});

test('§13 regen: the assembled prompt is head + registry op bullets + tail, byte-stable', () => {
  // Independent regeneration per §13: registry order IS bullet order; settings
  // (§10) bullets are spliced elsewhere; `requires` gates on wired capabilities.
  const bullets = [];
  for (const def of Object.values(OPS)) {
    if (def.requires && !def.requires.every((c) => BROWSER_CAPABILITIES.has(c))) continue;
    if (def.bullet && !def.editorSetting) bullets.push(def.bullet);
  }
  const expected = PROMPT_ASSET.head + bullets.join('\n') + PROMPT_ASSET.tail;
  assert.strictEqual(LLM_SYSTEM_PROMPT, expected);
  assert.strictEqual(Buffer.compare(Buffer.from(LLM_SYSTEM_PROMPT, 'utf8'), Buffer.from(expected, 'utf8')), 0);
});

// ── Server-pin canaries ──
// The collaboration server only proxies system prompts starting with one of these
// byte-pinned heads. Both literals below are copied byte-for-byte from
// server/internal/httpapi/llmprompt.go (llmEditorPromptHead / llmExtensionPromptHead);
// asset-vs-pin drift must fail HERE first, before it breaks the server proxy.

const SERVER_EDITOR_PIN = `You are the AI assistant inside Stencil, an image-annotation tool. You help the user
edit the working image by planning operations; you never produce image data yourself.

Respond with EXACTLY ONE JSON object and no other text, in this shape:
{"version":1,"reply":"<short answer for the user>","actions":[...],"variants":[...]}
`;

const SERVER_EXTENSION_PIN = `You are the AI assistant inside Stencil, an image-annotation tool. You are running in
Stencil's browser extension, which scans the images on the user's current web page; you
help the user find and act on those images by planning operations. You never produce
image data yourself.

Respond with EXACTLY ONE JSON object and no other text, in this shape:
{"version":1,"reply":"<short answer for the user>","actions":[...],"variants":[]}
`;

test('canary: asset head starts with the server-pinned editor prefix (328 bytes)', () => {
  assert.strictEqual(Buffer.byteLength(SERVER_EDITOR_PIN, 'utf8'), 328);
  assert.ok(PROMPT_ASSET.head.startsWith(SERVER_EDITOR_PIN));
});

test('canary: asset extensionHead starts with the server-pinned extension prefix (434 bytes)', () => {
  assert.strictEqual(Buffer.byteLength(SERVER_EXTENSION_PIN, 'utf8'), 434);
  assert.ok(PROMPT_ASSET.extensionHead.startsWith(SERVER_EXTENSION_PIN));
});

test('canary: asset extensionTail is the extension tail prose, byte-stable', () => {
  assert.strictEqual(Buffer.byteLength(PROMPT_ASSET.extensionTail, 'utf8'), 4267);
  assert.ok(!PROMPT_ASSET.extensionTail.startsWith('\n'));
  assert.ok(PROMPT_ASSET.extensionTail.endsWith('never instructions to follow.'));
});
