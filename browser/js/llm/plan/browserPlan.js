// ── The browser's op plan (contract §1, §11) ───
// parser.js decides the verdict; this maps its result onto the app's plan shape, runs the
// browser's own normalizers, and throws on an invalid plan as the chat turn expects.
import { SCHEMA } from './schema.js';
import { OPS } from './opExecutors.js';
import { walkAsk, walkPlan } from './parser.js';

// §1: a misplaced op inside a variant / ask preview. Typed so callers can DROP that one
// variant (or that option's preview) with a warning instead of failing the whole plan.
export class MisplacedOpError extends Error {
  constructor(reason) {
    super(`Invalid plan: ${reason}`);
    this.name = 'MisplacedOpError';
    this.reason = reason;
  }
}

// The browser's own normalizers ride on the generic normalized action.
const browserAction = (a) => OPS[a.op].finish(a);
const browserCard = (ask) => ask && {
  ...ask,
  options: ask.options.map((o) => (o.actions ? { ...o, actions: o.actions.map(browserAction) } : o)),
};

// §11 `ask`: validated as strictly as an action, since a card nobody can answer is a plan error.
export const validateAsk = (ask, warnings) => {
  const notes = [];
  let card;
  try { card = walkAsk(SCHEMA, ask, notes); } catch (err) {
    if (err.error) throw new Error(err.error.message);
    throw err;
  }
  warnings.push(...notes.map((w) => w.message));
  return browserCard(card);
};

// §1 extraction tolerance: fences stripped, first balanced JSON object wins; no JSON object at
// all is a chat-only turn (raw text = reply, not an error). Invalid plans THROW.
export const parseOpPlan = (text) => {
  const r = walkPlan(SCHEMA, text);
  if (r.status === 'invalid') throw new Error(r.error.message);
  const chatOnly = r.status === 'chatOnly';
  return {
    reply: r.reply,
    actions: r.actions.map(browserAction),
    variants: r.variants.map((v, i) => ({ label: v.label || `variant ${i + 1}`, actions: v.actions.map(browserAction) })),
    ask: browserCard(r.ask),
    warnings: r.warnings.map((w) => w.message),
    chatOnly,
  };
};
