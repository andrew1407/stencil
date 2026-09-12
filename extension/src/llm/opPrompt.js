// ── Op-plan: §13 system-prompt assembly ──
// The prose core is data; the "Available ops" section is GENERATED from OP_REGISTRY, so
// the prompt can never promise an op this surface does not register.
import { OP_REGISTRY } from './opValidate.js';

// System prompt (contract §4 two-part rule + §13): the PROSE CORE is data —
// src/config/systemPrompt.json (MV3 ships self-contained; drift-guarded against the
// browser copy by tests/dataParity.test.js). The "Available ops" section is GENERATED
// from OP_REGISTRY further down, so the prompt can never promise an op this surface
// does not register. Clients may append a short dynamic suffix, never prepend anything.
import PROMPT_ASSET from '../config/systemPrompt.json' with { type: 'json' };

const PROMPT_CORE_HEAD = PROMPT_ASSET.extensionHead;
// Prose core, part two — everything after the ops list (the ask paragraph, outlining
// anatomy, colour rules, the chat-only fallback and the injection guard).
const PROMPT_CORE_TAIL = PROMPT_ASSET.extensionTail;

// ── §13 prompt generation ──
// A registry bullet must never smuggle credentials or endpoint-setting text into the
// prompt: assembly fails loudly on these patterns instead of leaking. (The bare word
// "tokens" is fine — crop's unit tokens — so the token pattern is auth-qualified.)
const SENSITIVE_BULLET_PATTERNS = [
  /api[\s_-]?key/i,
  /\bbearer\b/i,
  /(auth|access|secret|session)[\s_-]?token/i,
  /\bendpoint\b/i,
];
const censorBullet = (name, bullet) => {
  for (const re of SENSITIVE_BULLET_PATTERNS) {
    if (re.test(bullet)) throw new Error(`Refusing to emit the prompt bullet for "${name}": it matches the sensitive pattern ${re}`);
  }
  return bullet;
};

// Assemble the prompt from the prose core + the registry's bullets, in order. §13
// capability truth: `exclude` names ops whose runtime capability is not wired on this
// surface — their bullets are omitted, so the model is never promised them. The shipped
// extension wires every registered op, so LLM_SYSTEM_PROMPT excludes nothing.
export const buildSystemPrompt = (registry = OP_REGISTRY, { exclude = new Set() } = {}) => {
  const ops = registry.filter((e) => !exclude.has(e.name))
    .map((e) => censorBullet(e.name, e.bullet)).join('\n');
  return `${PROMPT_CORE_HEAD}\n${ops}\n\n${PROMPT_CORE_TAIL}`;
};

export const LLM_SYSTEM_PROMPT = buildSystemPrompt();
