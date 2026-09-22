// Byte-identical copy of browser/js/llm/plan/opSchemaBase.js (portParity.test.js): the closure-free
// base of schema.js — value predicates, the SchemaError, message paths and the native rules.
// It keeps the original's name: the pin compares import specifiers by basename.

export const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
export const isFiniteNum = (v) => typeof v === 'number' && Number.isFinite(v);
export const isInt = (v) => isFiniteNum(v) && Math.floor(v) === v;
export const quoteList = (xs) => xs.map((x) => (typeof x === 'string' ? `"${x}"` : String(x))).join(', ');

// Thrown inside the checks; the public entry points re-throw it with the surface's
// "Invalid <op> action:" / "Invalid plan:" prefix.
export class SchemaError extends Error {}
export const bad = (why) => { throw new SchemaError(why); };

// Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]`.
export const where = (p) => (p.key ? `${p.container ? `${p.container}.` : ''}${p.root}${p.key}` : p.root.replace(/\.$/, ''));
export const label = (p) => `"${p.root}${p.key}"${p.container ? ` in ${p.container}` : ''}`;
export const child = (p, key) => (p && p.key ? { root: '', key, container: where(p) } : { root: p ? p.root : '', key, container: null });
export const item = (p, i) => ({ root: p.root, key: `${p.key}[${i}]`, container: p.container });

// ── native cross-field rules an entry may name in `rules` ───────────────────
export const RULES = {
  // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
  // conflicting duplicate fails. The folded copy is what gets validated + normalized.
  cropAspectFold(a) {
    if (a.aspect == null || !isObj(a.spec)) return a;
    const spec = { ...a.spec };
    if (spec.aspect != null && spec.aspect !== a.aspect) bad('"aspect" appears both beside "spec" and inside it with different values');
    if (spec.aspect == null) spec.aspect = a.aspect;
    const { aspect, ...rest } = a;
    return { ...rest, spec };
  },
};
