// ── Parsing + validating a model's op plan (contract §1, §11) ───
// Model output is DATA: everything here rejects or drops, never trusts. The registry
// decides what an op IS; this decides where it may sit.
import { SCHEMA, LIMITS, ASK_LIMITS, DEFAULT_CUSTOM_LABEL } from './schema.js';
import { isObj, isStr } from './values.js';
import { OPS } from './opExecutors.js';

// Derived from the registry: one appearing inside a variant fails the WHOLE plan.
const EDITOR_SETTINGS_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting));
// §2.1 image/save ride the same top-level-only enforcement (the message differs).
const TOP_LEVEL_ONLY_OPS = new Set(Object.keys(OPS).filter((op) => OPS[op].editorSetting || OPS[op].topLevelOnly));

// §1: a misplaced op inside a variant / ask preview. Typed so callers can DROP that one
// variant (or that option's preview) with a warning instead of failing the whole plan.
export class MisplacedOpError extends Error {
  constructor(reason) {
    super(`Invalid plan: ${reason}`);
    this.name = 'MisplacedOpError';
    this.reason = reason;
  }
}

// Unknown ops drop with a warning (forward compatibility); a known op with invalid params throws
// and nothing executes (contract §1). `scope` non-null names the variant/preview it landed in.
const validateActions = (list, warnings, where, scope = null) => {
  if (list == null) return [];
  if (!Array.isArray(list)) throw new Error(`Invalid plan: ${where} must be an array`);
  if (list.length > LIMITS.actions) throw new Error(`Invalid plan: more than ${LIMITS.actions} actions in ${where}`);
  const out = [];
  for (const a of list) {
    if (!isObj(a) || typeof a.op !== 'string') throw new Error(`Invalid plan: every action in ${where} must be an object with an "op"`);
    if (scope && TOP_LEVEL_ONLY_OPS.has(a.op)) {
      throw new MisplacedOpError(EDITOR_SETTINGS_OPS.has(a.op)
        ? `editor-settings op "${a.op}" is not allowed inside ${scope}`
          + (a.op === 'openUrl' ? " — open the URL as a top-level action; picking images off a web page is the extension assistant's job" : '')
        : a.op === 'undo' || a.op === 'redo'
          ? `"${a.op}" steps the live edit history — a top-level action only, not allowed inside variants or previews`
          : `"${a.op}" is a top-level action only (§2.1) — not allowed inside ${scope}`);
    }
    const def = OPS[a.op];
    if (!def) { warnings.push(`Skipped unknown operation "${a.op}"`); continue; }
    out.push(def.validate(a));
  }
  return out;
};

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

// §11 `ask`: validated as strictly as an action, since a card nobody can answer is a plan error.
// Its STRUCTURE is the registry's ask schema; only the preview actions need this module.
export const validateAsk = (ask, warnings) => {
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
      // Preview actions are RENDERED never executed, so editor-settings ops are rejected as if inside a
      // variant. §1 leniency: a misplaced op costs this option its PICTURE, not the plan.
      if (opt.actions != null) {
        try {
          out.actions = validateActions(opt.actions, warnings, where, 'variants or previews');
        } catch (err) {
          if (!(err instanceof MisplacedOpError)) throw err;
          warnings.push(`Dropped the preview for ${where} ("${out.label}") — ${err.reason}; the option is still offered`);
        }
      }
      // The client resolves the reference (or renders the option pictureless); nothing
      // here fetches anything.
      if (opt.image != null) out.image = card.options[i].image;
      return out;
    }),
  };
};

// The text an answered card sends as the user's next turn: the picked labels joined, or the
// typed custom text. Trimmed and capped so a pasted essay can't ride back as one "answer".
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  const labels = (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim());
  return labels.join(', ').slice(0, ASK_LIMITS.answer);
};

// §1 extraction tolerance: fences stripped, first balanced JSON object wins; no JSON object at
// all is a chat-only turn (raw text = reply, not an error). Invalid plans THROW.
export const parseOpPlan = (text) => {
  const raw = String(text == null ? '' : text);
  const chatOnly = () => ({ reply: raw.trim(), actions: [], variants: [], ask: null, warnings: [], chatOnly: true });
  const candidate = firstJsonObject(raw.replace(/```[a-zA-Z]*/g, ''));
  if (candidate == null) return chatOnly();
  let obj;
  try { obj = JSON.parse(candidate); } catch { return chatOnly(); }   // not actually JSON → chat-only

  // `version` other than 1 (or absent) is accepted but ignored.
  const warnings = [];
  // §1 reply tolerance: models routinely omit the reply while planning valid
  // actions — substitute rather than lose the plan to a missing pleasantry.
  const replyOmitted = typeof obj.reply !== 'string' || !obj.reply.trim();
  let reply = replyOmitted ? '' : obj.reply;
  const actions = validateActions(obj.actions, warnings, '"actions"');
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');
  const rawVariants = obj.variants || [];
  if (rawVariants.length > LIMITS.variants) throw new Error(`Invalid plan: more than ${LIMITS.variants} variants`);
  // §1 leniency: a variant holding a top-level-only/settings op is DROPPED with a
  // warning naming it — the top-level actions and the well-formed variants still run.
  const variants = [];
  rawVariants.forEach((v, i) => {
    if (!isObj(v)) throw new Error('Invalid plan: every variant must be an object');
    if (v.label != null && !isStr(v.label)) throw new Error('Invalid plan: variant "label" must be a string');
    const label = v.label || `variant ${i + 1}`;
    try {
      variants.push({ label, actions: validateActions(v.actions, warnings, `variant ${i + 1}`, 'variants') });
    } catch (err) {
      if (!(err instanceof MisplacedOpError)) throw err;
      warnings.push(`Dropped variant ${i + 1} ("${label}") — ${err.reason}; the rest of the plan ran`);
    }
  });
  const ask = validateAsk(obj.ask, warnings);
  // The substitute must not overstate what happened: "Done." only when the plan carries work, or
  // it reads as a success that never occurred.
  if (replyOmitted) {
    if (actions.length || variants.length || ask) {
      reply = 'Done.';
      warnings.push('The model omitted its reply — the plan still ran');
    } else {
      reply = 'The model returned an empty plan — nothing was changed.';
    }
  }
  return { reply, actions, variants, ask, warnings, chatOnly: false };
};
