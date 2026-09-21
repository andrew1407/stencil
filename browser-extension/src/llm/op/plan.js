// ── Op-plan: extension profile (llm-contract.md §1–§4 + §8 + §13) ──────
// Pure module — no DOM, no chrome, no fetch. The extension's op set references images by index
// in the context listing; `open.actions` carries the §2 core ops, validated with the same rules
// as browser/js/llm/plan/plan.js. LLM output is data, not instructions: plans are strictly
// validated before anything executes.
import {
  ASK_LIMITS, CORE_OPS, DEFAULT_CUSTOM_LABEL, FORBIDDEN_OPS, LIMITS, SCHEMA, isObj, isStr,
} from './profile.js';
import { EXT_VALIDATORS, GATHER_OPS, OP_REGISTRY } from './validate.js';
import { LLM_SYSTEM_PROMPT, buildSystemPrompt } from './prompt.js';

export {
  ASK_LIMITS, DEFAULT_CUSTOM_LABEL, FORBIDDEN_OPS, LIMITS, SCHEMA,
  LLM_SYSTEM_PROMPT, OP_REGISTRY, buildSystemPrompt,
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

// §8 extension rules: §2 core ops at the top level DROP with a warning, and so do "variants"
// (§1's leniency clause). An §11 ask option's preview can only be an existing image.scanIndex.
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

  // `version` other than 1 (or absent) is accepted but ignored. §1 reply tolerance: models
  // routinely omit the reply while planning valid actions, so substitute rather than lose the plan.
  let reply = obj.reply;
  const replyOmitted = typeof reply !== 'string' || !reply.trim();
  if (obj.variants != null && !Array.isArray(obj.variants)) throw new Error('Invalid plan: "variants" must be an array');

  const warnings = [];
  // §1's leniency clause applied to §8's "variants must be empty": the extension has none to
  // render, and losing the whole turn to a misplaced one costs the user everything.
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
  // The substitute must not overstate what happened: "Done." only when the plan carries work, or
  // it reads as a success that never occurred.
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

// True when every action only GATHERS context (the registry's `gather` flag), so the plan needs
// another model round — the same single bounded auto-continuation attach-only plans get (§8).
export const continuationOnly = (plan) =>
  !!plan && Array.isArray(plan.actions) && plan.actions.length > 0
  && plan.actions.every((a) => GATHER_OPS.has(a.op));
