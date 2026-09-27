// The closure-free base of schema.js: the value predicates, the SchemaError `bad` throws, the
// message-path builders and the native cross-field `rules`. Pure. Byte-pinned to the extension's.

export const isObj = (v) => v != null && typeof v === 'object' && !Array.isArray(v);
export const isFiniteNum = (v) => typeof v === 'number' && Number.isFinite(v);
export const isInt = (v) => isFiniteNum(v) && Math.floor(v) === v;
export const quoteList = (xs) => xs.map((x) => (typeof x === 'string' ? `"${x}"` : String(x))).join(', ');
// Model keys like "constructor" or "__proto__" must never reach Object.prototype.
export const own = (o, k) => (o != null && Object.hasOwn(o, k) ? o[k] : undefined);

// Thrown inside the checks; the public entry points re-throw it with the surface's
// "Invalid <op> action:" / "Invalid plan:" prefix.
export class SchemaError extends Error {}
export const bad = (why) => { throw new SchemaError(why); };

// Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]`.
export const where = (p) => (p.key ? `${p.container ? `${p.container}.` : ''}${p.root}${p.key}` : p.root.replace(/\.$/, ''));
export const label = (p) => `"${p.root}${p.key}"${p.container ? ` in ${p.container}` : ''}`;
export const child = (p, key) => (p && p.key ? { root: '', key, container: where(p) } : { root: p ? p.root : '', key, container: null });
export const item = (p, i) => ({ root: p.root, key: `${p.key}[${i}]`, container: p.container });

// ── normalization: the declared keys only, defaults applied, trims honoured ──
const pick = (v, spec) => {
  if (spec.type === 'object' && spec.fields && isObj(v)) return pickFields(v, spec.fields);
  if (spec.type === 'array' && Array.isArray(v)) return spec.items ? v.map((x) => pick(x, spec.items)) : v.slice();
  if (spec.type === 'string' && spec.trim && typeof v === 'string') return v.trim();
  return v;
};
export const pickFields = (obj, fields) => {
  const out = {};
  for (const [k, spec] of Object.entries(fields)) {
    if (own(obj, k) != null) out[k] = pick(obj[k], spec);
    else if (spec.default !== undefined) out[k] = spec.default;
  }
  return out;
};

// ── native cross-field rules an entry may name in `rules` ───────────────────
export const RULES = {
  // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
  // conflicting duplicate fails. The folded copy is what gets validated + normalized.
  cropAspectFold(a) {
    if (own(a, 'aspect') == null || !isObj(own(a, 'spec'))) return a;
    const spec = { ...a.spec };
    if (own(spec, 'aspect') != null && spec.aspect !== a.aspect) bad('"aspect" appears both beside "spec" and inside it with different values');
    if (own(spec, 'aspect') == null) spec.aspect = a.aspect;
    const { aspect, ...rest } = a;
    return { ...rest, spec };
  },
};

// ── registry `surfaceRules`: a surface's extra checks on the normalized action ──
const extensionOf = (path) => {
  const dot = path.lastIndexOf('.');
  return dot < 0 || dot < Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\')) ? null : path.slice(dot + 1);
};
export const SURFACE_RULES = Object.freeze({
  // At least one of `keys` inside the object at `key` (the console's crop needs an edge).
  anyOf: (out, rule) => rule.keys.some((k) => own(own(out, rule.key), k) != null),
  // A colour NAME the core's table knows (the grammar already admitted #rrggbb).
  knownColor: (out, rule, natives) => {
    const v = own(out, rule.key);
    return typeof v !== 'string' || v.startsWith('#') || natives.knownColor(v);
  },
  // A local path in a format the surface opens, judged by its extension.
  pathExtension: (out, rule) => {
    const ext = extensionOf(String(own(out, rule.key)));
    return ext != null && rule.extensions.includes(ext.replace(/[A-Z]/g, (c) => c.toLowerCase()));
  },
});
