// ── Registry-driven op-plan validation (llm-contract.md §1–§2, §8, §11) ──────
// The generic half of every op validator, table-driven from config/llm/opRegistry.json: profile
// membership, unknown-field rejection, required keys, types, enums, ranges, string caps, token
// grammars and the cross-field presence rules. Surfaces keep only their normalizers, executors
// and native `rules`. Pure. Byte-pinned to browser-extension/src/llm/op/schema.js.

import { RULES, SURFACE_RULES, SchemaError, bad, child, isFiniteNum, isInt, isObj, item, label, own, pickFields,
  quoteList, where } from './opSchemaBase.js';

// `capabilities` (absent = all) gates an entry's `requires`; `knownColor` backs that surface rule.
export const createSchema = (registry, surface, { capabilities, knownColor } = {}) => {
  const profile = own(registry.$meta.surfaceProfiles, surface);
  if (!profile) throw new Error(`opRegistry: unknown surface "${surface}"`);
  const limits = registry.limits;
  // A cap is a number or a dotted name into `limits` ("MAX_ACTIONS", "ask.label").
  const limit = (v) => {
    if (typeof v === 'number') return v;
    const n = String(v).split('.').reduce((o, k) => (o == null ? undefined : o[k]), limits);
    if (typeof n !== 'number') throw new Error(`opRegistry: unknown limit "${v}"`);
    return n;
  };
  const regexes = {};
  for (const [name, src] of Object.entries(registry.regexes)) {
    if (name !== 'describe' && name !== 'note') regexes[name] = new RegExp(src);
  }
  const describe = (name) => registry.regexes.describe[name] || name;

  // This surface's registered entries: its profile's ops in prompt order, minus entries restricted
  // to other surfaces, each resolved via surfaceKeys / bulletVariants / surfaceFlags.
  const order = registry.profiles[profile].ops;
  const forSurface = (map) => (map && (map[surface] ?? map[profile])) ?? undefined;
  const wired = capabilities == null ? null : new Set(capabilities);
  const entries = registry.ops
    .filter((e) => e.profiles.includes(profile) && (!e.surfaces || e.surfaces.includes(surface))
      && (!wired || (e.requires || []).every((c) => wired.has(c))))
    .sort((a, b) => order.indexOf(a.name) - order.indexOf(b.name))
    .map((e) => {
      const variant = forSurface(e.bulletVariants);
      return {
        ...e,
        keys: (e.surfaceKeys && e.surfaceKeys[surface]) || e.keys,
        bullet: typeof variant === 'string' ? variant : e.bullet,
        flags: { ...(e.flags || {}), ...(forSurface(e.surfaceFlags) || {}) },
      };
    });
  const ops = new Map(entries.map((e) => [e.name, e]));
  const forbidden = new Set(registry.forbidden.perSurface[surface] || []);
  const hardFail = (registry.forbidden.hardFail || []).includes(surface);
  const surfaceRules = registry.surfaceRules?.[surface] || [];
  const natives = { knownColor };
  for (const r of surfaceRules) {
    if (!Object.hasOwn(SURFACE_RULES, r.rule)) throw new Error(`opRegistry: unknown surface rule "${r.rule}"`);
    if (r.rule === 'knownColor' && typeof knownColor !== 'function') throw new Error('opRegistry: knownColor needs a colour table');
  }

  // ── value checks ──────────────────────────────────────────────────────────
  const checkString = (v, spec, path, parent) => {
    if (typeof v !== 'string') bad(`${label(path)} must be a string`);
    const max = spec.maxChars != null ? limit(spec.maxChars) : limits.MAX_STRING_CHARS;
    if (v.length > max) bad(`${label(path)} is longer than ${max} characters`);
    const s = spec.trim ? v.trim() : v;
    if (spec.nonEmpty && !s.trim()) bad(`${label(path)} must be a non-empty string`);
    if (spec.enum && !spec.enum.includes(s)) bad(`${label(path)} must be one of ${quoteList(spec.enum)}`);
    if (spec.literals && spec.literals.includes(s)) return;
    if (spec.blankOk && !s.trim()) return;
    let names = [].concat(spec.regex || []);
    if (spec.regexBy) {
      const by = own(parent, spec.regexBy.key);
      names = typeof by === 'string' && own(spec.regexBy.map, by) ? [spec.regexBy.map[by]] : [];
    }
    if (names.length && !names.some((n) => regexes[n].test(s))) {
      bad(`${label(path)} must be ${names.map(describe).join(' or ')}`);
    }
    if (spec.regexNot && regexes[spec.regexNot].test(s)) bad(`${label(path)} must be a local value, not ${describe(spec.regexNot)}`);
  };

  const checkNumber = (v, spec, path) => {
    const integer = spec.type === 'integer';
    const noun = integer ? 'an integer' : 'a number';
    if (!(integer ? isInt(v) : isFiniteNum(v))) bad(`${label(path)} must be ${noun}`);
    if (spec.enum && !spec.enum.includes(v)) bad(`${label(path)} must be one of ${quoteList(spec.enum)}`);
    if (spec.range) {
      const [lo, hi] = spec.range;
      if ((lo != null && v < lo) || (hi != null && v > hi)) {
        const range = lo != null && hi != null ? `${lo}..${hi}` : lo != null ? `>= ${lo}` : `<= ${hi}`;
        bad(`${label(path)} must be ${noun} ${range}`);
      }
    }
  };

  const checkBoolean = (v, spec, path) => {
    if (typeof v !== 'boolean') bad(`${label(path)} must be a boolean`);
    if (spec.enum && !spec.enum.includes(v)) bad(`${label(path)} must be ${quoteList(spec.enum)}`);
  };

  const checkArray = (v, spec, path) => {
    if (!Array.isArray(v)) bad(`${label(path)} must be an array`);
    const min = spec.minItems != null ? limit(spec.minItems) : null;
    const max = spec.maxItems != null ? limit(spec.maxItems) : null;
    if (min === 1 && !v.length) bad(`${label(path)} must be a non-empty array`);
    const window = min != null && min > 1 && max != null;   // a real N..M window, not just a cap
    if (max != null && v.length > max) bad(window ? `${label(path)} must hold ${min}..${max} entries` : `more than ${max} entries in ${label(path)}`);
    if (min != null && v.length < min) bad(window ? `${label(path)} must hold ${min}..${max} entries` : `${label(path)} must hold at least ${min} entries`);
    if (spec.items) v.forEach((x, i) => checkValue(x, spec.items, item(path, i), null));
  };

  const checkObject = (v, spec, path) => {
    if (!isObj(v)) bad(`${label(path)} must be an object`);
    if (spec.fields || spec.minFields != null) checkFields(v, spec.fields || {}, spec, path, []);
  };

  const CHECKERS = Object.freeze({ string: checkString, integer: checkNumber, number: checkNumber,
    boolean: checkBoolean, array: checkArray, object: checkObject });
  const checkValue = (v, spec, path, parent) => {
    const check = Object.hasOwn(CHECKERS, spec.type) ? CHECKERS[spec.type] : null;
    if (!check) throw new Error(`opRegistry: unknown type "${spec.type}"`);
    return check(v, spec, path, parent);
  };

  // One object against a key map + its holder's presence rules. `skip` names keys
  // that are neither declared nor unknown (the action's own "op").
  const checkFields = (obj, fields, holder, path, skip) => {
    const at = (key) => child(path, key);
    const present = (k) => own(obj, k) != null;
    if (!holder.allowUnknown) {
      for (const k of Object.keys(obj)) {
        if (!skip.includes(k) && !Object.hasOwn(fields, k)) bad(`unknown field "${k}"${path ? ` in ${where(path)}` : ''}`);
      }
    }
    if (holder.forms) {
      const inForms = new Set(holder.forms.flat());
      const given = Object.keys(fields).filter((k) => inForms.has(k) && present(k));
      const matched = holder.forms.filter((f) => f.length === given.length && f.every((k) => given.includes(k)));
      if (matched.length !== 1) bad(`exactly one of ${holder.forms.map((f) => f.map((k) => `"${k}"`).join('+')).join(' / ')} is required`);
    }
    for (const group of holder.together || []) {
      const n = group.filter(present).length;
      if (n && n !== group.length) bad(`${group.map((k) => `"${k}"`).join(' and ')} ride together`);
    }
    for (const group of holder.exclusive || []) {
      if (group.filter(present).length > 1) bad(`carries both ${group.map((k) => `"${k}"`).join(' and ')} — at most one of them`);
    }
    if (holder.minFields != null) {
      const n = Object.keys(fields).filter(present).length;
      if (n < holder.minFields) bad(`needs at least ${holder.minFields === 1 ? 'one' : holder.minFields} of ${Object.keys(fields).join('/')}`);
    }
    for (const [k, spec] of Object.entries(fields)) {
      if (!present(k)) {
        if (spec.required) bad(`${label(at(k))} is required`);
        if (spec.requiredWith) {
          for (const [dep, vals] of Object.entries(spec.requiredWith)) {
            if (vals.includes(own(obj, dep))) bad(`${label(at(k))} is required with "${dep}" ${quoteList([obj[dep]])}`);
          }
        }
        continue;
      }
      if (spec.onlyWith) {
        for (const [dep, vals] of Object.entries(spec.onlyWith)) {
          if (!vals.includes(own(obj, dep))) bad(`${label(at(k))} only applies with "${dep}" ${vals.map((x) => `"${x}"`).join(' or ')}`);
        }
      }
      checkValue(obj[k], spec, at(k), obj);
    }
  };

  // The model's fault carries its `detail` (the message sans prefix); a registry bug does not.
  const rethrow = (err, prefix) => {
    if (err instanceof SchemaError) throw Object.assign(new Error(`${prefix}${err.message}`), { detail: err.message });
    throw err;
  };

  // Natives rules first; returns the action as validated (post-fold) — feed it to `normalize`.
  const validateAction = (a, entry) => {
    try {
      let v = a;
      for (const rule of entry.rules || []) v = RULES[rule](v);
      checkFields(v, entry.keys, entry, null, ['op']);
      return v;
    } catch (err) { return rethrow(err, `Invalid ${entry.name} action: `); }
  };
  // { op, ...declared keys present (deep-picked), defaults }.
  const normalize = (v, entry) => ({ op: entry.name, ...pickFields(v, entry.keys) });

  // ── public surface ────────────────────────────────────────────────────────
  return {
    surface, profile, limits, entries, ops, forbidden, hardFail, surfaceRules, regexes,
    jsonCaps: limits.json ? { depth: limits.json.MAX_DEPTH, bytes: limits.json.MAX_BYTES, nodes: limits.json.MAX_NODES } : null,
    defaultCustomLabel: registry.ask.defaultCustomLabel,
    limit, validateAction, normalize,
    // validateAction → normalize → this surface's `surfaceRules` on the normalized action.
    accept(a, entry) {
      const out = normalize(validateAction(a, entry), entry);
      try {
        for (const r of surfaceRules) {
          if (r.op === entry.name && !SURFACE_RULES[r.rule](out, r, natives)) bad(r.message.replace('{value}', () => String(own(out, r.key))));
        }
      } catch (err) { return rethrow(err, `Invalid ${entry.name} action: `); }
      return out;
    },
    // The §11 card's structure (option `actions` only shallowly — the caller
    // validates them as preview actions). Throws "Invalid plan: …".
    validateAsk(ask) {
      try {
        if (!isObj(ask)) bad('"ask" must be an object');
        const schema = registry.ask.schema;
        checkFields(ask, schema.keys, schema, { root: 'ask.', key: '', container: null }, []);
      } catch (err) { return rethrow(err, 'Invalid plan: '); }
    },
    normalizeAsk(ask) { return pickFields(ask, registry.ask.schema.keys); },
    // Resolve an op inside a nested op set (§8 open.actions): an entry to validate
    // with, "fail" for a listed-but-disallowed op, or null for an unknown op.
    opsetEntry(name, op) {
      const os = registry.opsets[name];
      if (!os) throw new Error(`opRegistry: unknown opset "${name}"`);
      if (own(os.overrides, op)) return { name: op, keys: os.overrides[op].keys, rules: os.overrides[op].rules || [] };
      if (os.ops.includes(op)) return registry.ops.find((e) => e.id === op) || null;
      if (os.failOps && os.failOps.includes(op)) return 'fail';
      return null;
    },
    envelope: registry.envelope,
    // Check one envelope slot ("actions" / "variants") shallowly.
    checkEnvelope(v, key) {
      try { checkValue(v, registry.envelope[key], { root: '', key, container: null }, null); }
      catch (err) { return rethrow(err, 'Invalid plan: '); }
    },
  };
};
