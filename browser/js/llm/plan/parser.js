// ── Parsing + validating a model's op plan (llm-contract.md §1, §11, §13) ───
// Model text → ONE result {status, reply, actions, variants, ask, warnings, error}; model output
// is DATA, so nothing here throws on it. The JS twin of core/opplan/planWalk.cpp, pinned
// byte-for-byte by fixtures/opPlan/generated/normalized.json; browserPlan.js maps it for the app.
import { ASK_LIMITS } from './schema.js';
import { isObj, own } from './opSchemaBase.js';
import { isStr } from './values.js';
import { jsonLimit } from './planCaps.js';

// §1 extraction tolerance: Markdown fences go, whatever language tag they carry.
const stripFences = (text) => text.replace(/```[a-zA-Z]*/g, '');

// The first balanced { … } object (string-aware) in the text, or null.
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

// The text an answered card sends as the user's next turn: the picked labels joined, or the
// typed custom text. Trimmed and capped so a pasted essay can't ride back as one "answer".
export const askAnswerText = (ask, { picked = [], custom = '' } = {}) => {
  const typed = String(custom || '').trim();
  if (typed) return typed.slice(0, ASK_LIMITS.answer);
  const labels = (Array.isArray(picked) ? picked : [picked])
    .map((p) => (isObj(p) ? p.label : p)).filter((l) => isStr(l) && l.trim());
  return labels.join(', ').slice(0, ASK_LIMITS.answer);
};

// A failed plan, carried out of the walk as data; the fields follow core's key order.
class PlanFail {
  constructor(code, fields, detail) {
    this.error = { code, ...fields, detail, message: `Invalid plan: ${detail}` };
  }
}
const planFail = (fields, detail) => { throw new PlanFail('E_PLAN', fields, detail); };
const warn = (code, fields, message) => ({ code, ...fields, message });

const isSettings = (e) => !!(e.flags.editorSetting || e.flags.consoleSetting);
const isTopLevel = (e) => !!e.flags.topLevelOnly || isSettings(e);

// §1: why a registered op may not sit inside a variant or a preview.
const misplacedReason = (e, scope) => (isSettings(e)
  ? `editor-settings op "${e.name}" is not allowed inside ${scope}`
    + (e.name === 'openUrl' ? " — open the URL as a top-level action; picking images off a web page is the extension assistant's job" : '')
  : e.name === 'undo' || e.name === 'redo'
    ? `"${e.name}" steps the live edit history — a top-level action only, not allowed inside variants or previews`
    : `"${e.name}" is a top-level action only (§2.1) — not allowed inside ${scope}`);

// An action's own failure, re-thrown as the plan's error.
const accept = (schema, a, entry) => {
  try { return schema.accept(a, entry); } catch (err) {
    if (err.detail === undefined) throw err;
    const fail = new PlanFail('E_ACTION', { op: entry.name }, err.detail);
    fail.error.message = err.message;
    throw fail;
  }
};

// One actions list. `nested` = { scope, index, reason } inside a variant or a preview, where a
// top-level op returns { misplaced } for the caller to drop; unknown ops warn and skip.
const walkActions = (schema, list, warnings, nested = null) => {
  const scopeFields = nested ? { scope: nested.scope, index: nested.index } : { scope: 'actions' };
  const where = nested ? `${nested.scope === 'variant' ? 'variant' : 'ask option'} ${nested.index}` : '"actions"';
  if (list == null) return { actions: [] };
  if (!Array.isArray(list)) planFail({ rule: 'notArray', ...scopeFields }, `${where} must be an array`);
  const max = schema.limits.MAX_ACTIONS;
  if (list.length > max) planFail({ rule: 'tooMany', ...scopeFields, max }, `more than ${max} actions in ${where}`);
  const actions = [];
  for (const [item, a] of list.entries()) {
    if (!isObj(a) || typeof own(a, 'op') !== 'string') {
      planFail({ rule: 'notAction', ...scopeFields, item, isObject: isObj(a) }, `every action in ${where} must be an object with an "op"`);
    }
    if (schema.hardFail && schema.forbidden.has(a.op)) {
      const detail = `the "${a.op}" op is never model-drivable`;
      throw new PlanFail('E_FORBIDDEN', { op: a.op }, detail);
    }
    const entry = schema.ops.get(a.op);
    if (!entry) { warnings.push(warn('W_UNKNOWN_OP', { op: a.op }, `Skipped unknown operation "${a.op}"`)); continue; }
    if (nested && isTopLevel(entry)) return { misplaced: entry, reason: misplacedReason(entry, nested.reason) };
    actions.push(accept(schema, a, entry));
  }
  return { actions };
};

// §1 variants: each must be an object whose label (when given) is a string; one holding a
// top-level op is DROPPED with a warning while the rest of the plan runs.
const walkVariants = (schema, raw, warnings) => {
  if (raw == null) return [];
  if (!Array.isArray(raw)) planFail({ rule: 'variantsNotArray' }, '"variants" must be an array');
  const max = schema.limits.MAX_VARIANTS;
  if (raw.length > max) planFail({ rule: 'tooManyVariants', max }, `more than ${max} variants`);
  const variants = [];
  for (const [item, v] of raw.entries()) {
    if (!isObj(v)) planFail({ rule: 'variantNotObject', item }, 'every variant must be an object');
    const given = own(v, 'label');
    if (given != null && !(typeof given === 'string' && given.length <= schema.limits.MAX_STRING_CHARS)) {
      planFail({ rule: 'variantLabel', item, isString: typeof given === 'string' }, 'variant "label" must be a string');
    }
    const index = item + 1;
    const walked = walkActions(schema, own(v, 'actions'), warnings, { scope: 'variant', index, reason: 'variants' });
    if (walked.misplaced) {
      const shown = given || `variant ${index}`;
      warnings.push(warn('W_VARIANT_DROPPED', { op: walked.misplaced.name, index, label: given ?? null },
        `Dropped variant ${index} ("${shown}") — ${walked.reason}; the rest of the plan ran`));
    } else variants.push({ label: given ?? null, actions: walked.actions });
  }
  return variants;
};

// §11 `ask`: its STRUCTURE is the registry's ask schema; a misplaced preview op costs that
// option its picture, never the plan. Throws PlanFail.
export const walkAsk = (schema, ask, warnings) => {
  if (ask == null) return null;
  try { schema.validateAsk(ask); } catch (err) {
    if (err.detail === undefined) throw err;
    planFail({ rule: 'ask' }, err.detail);
  }
  const card = schema.normalizeAsk(ask);
  const options = ask.options.map((opt, i) => {
    const out = { label: card.options[i].label };
    if (own(opt, 'actions') != null) {
      const walked = walkActions(schema, opt.actions, warnings, { scope: 'option', index: i + 1, reason: 'variants or previews' });
      if (walked.misplaced) {
        warnings.push(warn('W_PREVIEW_DROPPED', { op: walked.misplaced.name, index: i + 1, label: out.label },
          `Dropped the preview for ask option ${i + 1} ("${out.label}") — ${walked.reason}; the option is still offered`));
      } else out.actions = walked.actions;
    }
    if (own(opt, 'image') != null) out.image = card.options[i].image;
    return out;
  });
  return {
    question: card.question, mode: card.mode, allowCustom: card.allowCustom === true,
    customLabel: card.customLabel || schema.defaultCustomLabel, options,
  };
};

const result = (status, reply, fields = {}) => ({
  status, reply, actions: [], variants: [], ask: null, warnings: [], error: null, ...fields,
});

// §1 extraction tolerance, strict validation and reply tolerance, as one result document.
export const walkPlan = (schema, text) => {
  const raw = String(text ?? '');
  const candidate = firstJsonObject(stripFences(raw));
  if (candidate == null) return result('chatOnly', raw.trim());
  let obj;
  try { obj = JSON.parse(candidate); } catch { return result('chatOnly', raw.trim()); }
  try {
    const cap = jsonLimit(obj, candidate, schema.jsonCaps);
    if (cap) throw new PlanFail('E_JSON_LIMIT', { limit: cap.limit, max: cap.max }, cap.detail);
    const warnings = [];
    const reply = own(obj, 'reply');
    const replyOmitted = typeof reply !== 'string' || !reply.trim();
    const { actions } = walkActions(schema, own(obj, 'actions'), warnings);
    const variants = walkVariants(schema, own(obj, 'variants'), warnings);
    const ask = walkAsk(schema, own(obj, 'ask'), warnings);
    let said = replyOmitted ? 'The model returned an empty plan — nothing was changed.' : reply;
    // "Done." only when the plan carries work, or it reads as a success that never occurred.
    if (replyOmitted && (actions.length || variants.length || ask)) {
      said = 'Done.';
      warnings.push(warn('W_REPLY_OMITTED', {}, 'The model omitted its reply — the plan still ran'));
    }
    return result('valid', said, { actions, variants, ask, warnings });
  } catch (err) {
    if (err instanceof PlanFail) return result('invalid', '', { error: err.error });
    throw err;
  }
};
