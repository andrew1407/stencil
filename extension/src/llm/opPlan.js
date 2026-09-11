// ── Op-plan: extension profile (llm-contract.md §1–§4 + §8 + §13) ──────
// Pure module — no DOM, no chrome, no fetch. The extension's op set (contract §8)
// references images by index in the context listing; `open.actions` carries the §2
// core ops, validated with the same rules as browser/js/llm/opPlan.js (mirrored here,
// since the extension can't import from browser/ — keep them in sync). LLM output is
// data, not instructions: plans are strictly validated before anything executes.

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

import REGISTRY from '../config/opRegistry.json' with { type: 'json' };
import { createSchema } from './opSchema.js';

// The table this surface validates against: the checked-in copy of the shared
// config/llm/opRegistry.json (drift-guarded by tests/dataParity.test.js), filtered to the
// extension profile (contract §8). Membership, key schemas, caps and the cross-field
// rules come from the registry; this module keeps the listing-bound checks (an index
// must point into the scan / tab listing) and the normalizers.
export const SCHEMA = createSchema(REGISTRY, 'extension');

// Limits — the same numbers in every client (contract §1) + the §8 attach/pin caps and
// the panel-settings caps, all read from the registry.
export const LIMITS = {
  actions: SCHEMA.limits.MAX_ACTIONS, variants: SCHEMA.limits.MAX_VARIANTS,
  layoutLines: SCHEMA.limits.MAX_LAYOUT_LINES, stringChars: SCHEMA.limits.MAX_STRING_CHARS,
  attachIndices: SCHEMA.limits.extension.attachIndices, pinIndices: SCHEMA.limits.extension.pinIndices,
  filterSearch: SCHEMA.limits.extension.filterSearch, filterFormats: SCHEMA.limits.extension.filterFormats,
  filterSize: SCHEMA.limits.extension.filterSize,
};

// §11 interactive replies — the same numbers as every other client.
export const ASK_LIMITS = { ...SCHEMA.limits.ask };
export const DEFAULT_CUSTOM_LABEL = REGISTRY.ask.defaultCustomLabel;

// The full §2 core-op set: at the TOP level of an extension plan these are dropped
// with a warning (contract §8 — editing happens in the editor after `open`).
const CORE_OPS = new Set(REGISTRY.opsets.extensionOpen.failOps);

const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
const isStr = (v, max = LIMITS.stringChars) => typeof v === 'string' && v.length <= max;
const fail = (op, why) => { throw new Error(`Invalid ${op} action: ${why}`); };

// Table-driven check + normalize for one action against its registry entry.
const check = (a, entry) => SCHEMA.normalize(SCHEMA.validateAction(a, entry), entry);

// An image index (already an integer >= 0) must point into the current context listing.
const validIndex = (v, listingLength, op) => {
  if (v >= listingLength) fail(op, `image index ${v} is out of range (the listing has ${listingLength} entries)`);
  return v;
};

// The attach/pin/unpin shape (exactly one of `image` / `images`, capped by the
// registry) normalized to a bounded `indices` array.
const indicesOp = (entry, a, listingLength) => {
  const v = check(a, entry);
  const indices = v.image != null ? [v.image] : v.images;
  return { op: entry.name, indices: indices.map((i) => validIndex(i, listingLength, entry.name)) };
};

// The actions allowed inside `open` (§2 subset, registry opset "extensionOpen"). A §2 op
// OUTSIDE the subset is a known-but-invalid action → the whole plan fails; a truly
// unknown op drops with a warning (§1 forward compatibility).
const validateOpenActions = (list, warnings) => {
  const out = [];
  for (const a of list || []) {
    if (!isObj(a) || typeof a.op !== 'string') fail('open', 'every entry in "actions" must be an object with an "op"');
    const entry = SCHEMA.opsetEntry('extensionOpen', a.op);
    if (entry === 'fail') fail('open', `"${a.op}" is not allowed in open.actions (only crop/rotate/filter/layout/page)`);
    if (!entry) { warnings.push(`Skipped unknown operation "${a.op}" inside "open"`); continue; }
    out.push(check(a, entry));
  }
  return out;
};

// ── Per-op normalizers on top of the table-driven check ──
// validate(a, listingLength, warnings, tabsLength) → the cleaned action, or throw.
const VALIDATE = {
  focus: (entry, a, listingLength) => {
    const v = check(a, entry);
    return { op: 'focus', image: validIndex(v.image, listingLength, 'focus') };
  },
  open: (entry, a, listingLength, warnings) => {
    const v = check(a, entry);
    const out = { op: 'open', image: validIndex(v.image, listingLength, 'open'), actions: validateOpenActions(a.actions, warnings) };
    if (a.incognito != null) out.incognito = v.incognito;
    if (a.mode != null) out.mode = v.mode;
    return out;
  },
  attach: indicesOp,
  pin: indicesOp,
  unpin: indicesOp,
  scanTab: (entry, a, _listingLength, _warnings, tabsLength) => {
    const v = check(a, entry);
    if (!tabsLength) fail('scanTab', 'there are no other open tabs to scan');
    if (v.tab >= tabsLength) fail('scanTab', `tab index ${v.tab} is out of range (the tab list has ${tabsLength} entries)`);
    return v;
  },
  filter: (entry, a) => {
    const v = check(a, entry);
    if (v.kinds) v.kinds = [...new Set(v.kinds)];
    // Upper-cased here so the executor can match the panel's own pill labels.
    if (v.formats) v.formats = v.formats.map((f) => f.trim().toUpperCase()).filter(Boolean);
    return v;
  },
};

// ── The op registry (contract §8 + §13) ──
// One ordered entry per op this surface registers, assembled from the shared registry's
// extension entries: name + prompt bullet + flags (`gather` marks context-gathering ops —
// §8 auto-continuation; `panelSettings` the panel's own controls) + validate. The
// prompt's "Available ops" is assembled from the bullets in this order, and
// chatController.js's executors key on the same names.
export const OP_REGISTRY = SCHEMA.entries.map((entry) => {
  const def = {
    name: entry.name,
    bullet: entry.bullet,
    validate: (a, listingLength, warnings, tabsLength) =>
      (VALIDATE[entry.name] ? VALIDATE[entry.name](entry, a, listingLength, warnings, tabsLength) : check(a, entry)),
  };
  if (entry.flags && entry.flags.gather) def.gather = true;
  if (entry.flags && entry.flags.panelSettings) def.panelSettings = true;
  return def;
});

// Validator lookup for parseOpPlan, derived from the registry.
const EXT_VALIDATORS = Object.fromEntries(OP_REGISTRY.map((e) => [e.name, e.validate]));
// The gather set (§8 auto-continuation), likewise derived.
const GATHER_OPS = new Set(OP_REGISTRY.filter((e) => e.gather).map((e) => e.name));

// ── §13 forbidden ops ──
// Never model-drivable, on any surface: llm/provider configuration, clipboard reads,
// hotkey rebinding, session end, chat persistence/consent and server-side destruction —
// plus the §8 un-drivable extension surface (editor URL, Options, shareTabs, downloads).
// Teeth: a test pins that no registry entry uses these names, and chatController
// refuses them at execution even if one somehow appears.
export const FORBIDDEN_OPS = new Set(SCHEMA.forbidden);

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

// Take the first balanced { … } object (string-aware) from the text, or null.
const firstJsonObject = (text) => {
  const start = text.indexOf('{');
  if (start < 0) return null;
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const c = text[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c === '\\') esc = true;
      else if (c === '"') inStr = false;
      continue;
    }
    if (c === '"') inStr = true;
    else if (c === '{') depth++;
    else if (c === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
  return null;
};

// Parse the raw LLM reply into a validated extension plan { reply, actions, warnings,
// chatOnly } — see parseOpPlan below. Shared §1 mechanics: Markdown fences stripped,
// first balanced JSON object wins, no JSON at all = a chat-only turn (not an error).
// Extension rules (§8): §2 core ops at the top level DROP with a warning, and so do
// "variants" (§1's leniency clause). Invalid plans THROW.
// ── §11 interactive replies (`ask`) ─────────────────────────────────────────
// A question put back to the user as a choice card, validated as strictly as an action:
// null when absent. The card's STRUCTURE (keys, caps, the image reference's
// exactly-one-of url / projectId / scanIndex, http(s)-only urls) is the registry's ask
// schema; what this surface can SHOW is decided here — the extension is not an editor, so
// an option's PREVIEW can only be an existing image (`image.scanIndex`), and one carrying
// preview `actions` or a model-supplied `image.url` keeps its label but loses the picture,
// with a warning (§11.2).
export const validateAsk = (ask, listingLength, warnings) => {
  if (ask == null) return null;
  SCHEMA.validateAsk(ask);
  const card = SCHEMA.normalizeAsk(ask);
  return {
    question: card.question,
    mode: card.mode,
    allowCustom: card.allowCustom === true,
    customLabel: card.customLabel || DEFAULT_CUSTOM_LABEL,
    options: ask.options.map((opt, i) => {
      const where = `ask option ${i + 1}`;
      const out = { label: card.options[i].label };
      // No working image in the extension: the option stands, its preview doesn't.
      if (opt.actions != null) warnings.push(`${where}: this surface can't render a preview of an edit — the option is shown without one`);
      if (opt.image != null) {
        const img = card.options[i].image;
        if (img.scanIndex != null) {
          // An index past the listing is the model mis-numbering: keep the option, lose its picture.
          if (listingLength && img.scanIndex >= listingLength) warnings.push(`${where}: image ${img.scanIndex} is not in the list — the option is shown without a preview`);
          else out.image = { scanIndex: img.scanIndex };
        } else if (img.url != null) {
          // Never resolved (§11.2): loading it would make the popup — which holds <all_urls>
          // and the user's cookies — fetch a model-chosen host. Option kept, picture dropped.
          warnings.push(`${where}: an image URL from the model is not fetched here — the option is shown without a preview`);
        } else {
          out.image = { projectId: img.projectId };
        }
      }
      return out;
    }),
  };
};

// The text an answered card sends as the user's next turn.
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  const labels = (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim());
  return labels.join(', ').slice(0, ASK_LIMITS.answer);
};

export const parseOpPlan = (text, { listingLength = 0, tabsLength = 0 } = {}) => {
  const raw = String(text == null ? '' : text);
  const chatOnly = () => ({ reply: raw.trim(), actions: [], ask: null, warnings: [], chatOnly: true });
  const candidate = firstJsonObject(raw.replace(/```[a-zA-Z]*/g, ''));
  if (candidate == null) return chatOnly();
  let obj;
  try { obj = JSON.parse(candidate); } catch { return chatOnly(); }   // not actually JSON → chat-only

  // `version` other than 1 (or absent) is accepted but ignored.
  // §1 reply tolerance: models routinely omit the reply while planning valid
  // actions — substitute rather than lose the plan to a missing pleasantry.
  let reply = obj.reply;
  const replyOmitted = typeof reply !== 'string' || !reply.trim();
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');

  const warnings = [];
  // §1's leniency clause, applied to §8's "variants must be empty": the extension
  // has no variants to render, but losing the whole turn to a misplaced one costs
  // the user everything — drop them with a warning and run the rest.
  const droppedVariants = Array.isArray(obj.variants) ? obj.variants.length : 0;
  if (droppedVariants) {
    warnings.push(`Skipped ${droppedVariants} variant${droppedVariants === 1 ? '' : 's'} — the extension doesn't edit images, so it renders no variants; the rest of the plan ran`);
  }

  const list = obj.actions == null ? [] : obj.actions;
  if (!Array.isArray(list)) throw new Error('Invalid plan: "actions" must be an array');
  if (list.length > LIMITS.actions) throw new Error(`Invalid plan: more than ${LIMITS.actions} actions`);
  const actions = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') throw new Error('Invalid plan: every action must be an object with an "op"');
    const validate = EXT_VALIDATORS[a.op];
    if (validate) { actions.push(validate(a, listingLength, warnings, tabsLength)); continue; }
    // Core ops at the top level drop with a warning (§8); truly unknown ops drop
    // with the generic §1 forward-compatibility warning.
    if (CORE_OPS.has(a.op)) warnings.push(`Skipped core operation "${a.op}" — the extension doesn't edit images; use it inside an "open" action`);
    else warnings.push(`Skipped unknown operation "${a.op}"`);
  }
  const ask = validateAsk(obj.ask, listingLength, warnings);
  // The substitute must not overstate what happened: "Done." only when the plan
  // actually carries work — an empty plan says so, since a bare "Done." there
  // reads as a success that never occurred.
  if (replyOmitted) {
    if (actions.length || ask) {
      reply = 'Done.';
      warnings.push('The model omitted its reply — the plan still ran');
    } else {
      reply = 'The model returned an empty plan — nothing was changed.';
    }
  }
  return { reply, actions, ask, warnings, chatOnly: false };
};

// True when the plan's only actionable output is `attach` — the trigger for the
// single bounded auto-continuation (contract §8).
export const attachOnly = (plan) =>
  !!plan && Array.isArray(plan.actions) && plan.actions.length > 0
  && plan.actions.every((a) => a.op === 'attach');

// True when every action only GATHERS context (the registry's `gather` flag: attach /
// scanTab / rescan) — the plan needs another model round to act on what arrived, so it
// triggers the same single bounded auto-continuation as attach-only plans (contract §8).
export const continuationOnly = (plan) =>
  !!plan && Array.isArray(plan.actions) && plan.actions.length > 0
  && plan.actions.every((a) => GATHER_OPS.has(a.op));
