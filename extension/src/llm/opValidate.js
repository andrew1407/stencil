// ── Op-plan: the per-op validators and the op registry (contract §8 + §13) ──
// One ordered entry per op this surface registers: the table-driven check from
// opProfile.js, plus the listing-bound rules and normalizers only this surface has.
import { SCHEMA, check, fail, isObj, validIndex } from './opProfile.js';


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
export const EXT_VALIDATORS = Object.fromEntries(OP_REGISTRY.map((e) => [e.name, e.validate]));
// The gather set (§8 auto-continuation), likewise derived.
export const GATHER_OPS = new Set(OP_REGISTRY.filter((e) => e.gather).map((e) => e.name));
