// The op-plan corpus's MECHANICAL cases, derived from opRegistry.json: boundary pairs (N ok / N+1
// invalid), unknown fields, wrong types, missing required keys, enum/grammar rejections and the
// cross-field presence rules for every op × profile. genOpPlanFixtures.mjs writes them as
// js/config/llm/fixtures/opPlan/generated/cases.json; opPlanCardCases.mjs adds the envelope + §11 card.
import { cardCases } from './opPlanCardCases.mjs';

export const generate = (registry) => {
  const limits = registry.limits;
  const limit = (v) => (typeof v === 'number' ? v : String(v).split('.').reduce((o, k) => o[k], limits));
  const surfacesOf = (profile) => registry.profiles[profile].surfaces;
  const ALL = Object.keys(registry.profiles);

  // A sample that satisfies a key spec (the smallest shape that passes every rule).
  const SAMPLE_BY_REGEX = {
    CROP_TOKEN: '10%', CROP_ASPECT: '3:4', PAGE_FORMAT: 'a4', HEX: '#aabbcc', CSS_NAME: 'white',
    FORMULA_X: 'x*2', FORMULA_Y: 'y*2', HTTP_URL: 'https://example.com/a.png',
  };
  const sampleNumber = (spec) => {
    if (spec.enum) return spec.enum[0];
    const [lo, hi] = spec.range || [null, null];
    return lo ?? hi ?? 1;
  };
  const SAMPLERS = Object.freeze({
    string: (spec, parent) => {
      if (spec.enum) return spec.enum[0];
      if (spec.regexBy) return SAMPLE_BY_REGEX[spec.regexBy.map[parent[spec.regexBy.key]]] || 'x';
      const names = [].concat(spec.regex || []);
      if (names.length) return SAMPLE_BY_REGEX[names[0]];
      return spec.regexNot ? 'out.png' : 'x';
    },
    integer: sampleNumber,
    number: sampleNumber,
    boolean: (spec) => (spec.enum ? spec.enum[0] : true),
    array: (spec) => {
      const n = spec.minItems != null ? limit(spec.minItems) : 1;
      return Array.from({ length: n }, () => (spec.items ? sample(spec.items, {}) : 0));
    },
    object: (spec) => minimalObject(spec.fields || {}, spec),
  });
  const sample = (spec, parent) => {
    if (!Object.hasOwn(SAMPLERS, spec.type)) throw new Error(`unknown type ${spec.type}`);
    return SAMPLERS[spec.type](spec, parent);
  };
  // The smallest valid object for a key map + holder rules: required keys, the first
  // form, one key for minFields; onlyWith dependencies are satisfied when a key rides.
  const minimalObject = (fields, holder, want = []) => {
    const chosen = new Set(want);
    for (const [k, s] of Object.entries(fields)) if (s.required) chosen.add(k);
    if (holder.forms) {
      const form = holder.forms.find((f) => f.some((k) => chosen.has(k))) || holder.forms[0];
      for (const k of form) chosen.add(k);
    }
    if (holder.minFields && chosen.size < holder.minFields) {
      for (const k of Object.keys(fields)) { if (chosen.size >= holder.minFields) break; chosen.add(k); }
    }
    for (const group of holder.together || []) if (group.some((k) => chosen.has(k))) for (const k of group) chosen.add(k);
    const out = {};
    for (const k of Object.keys(fields)) {
      if (!chosen.has(k)) continue;
      const s = fields[k];
      for (const [dep, vals] of Object.entries(s.onlyWith || {})) out[dep] = vals[0];
    }
    for (const k of Object.keys(fields)) {
      if (chosen.has(k) && out[k] === undefined) out[k] = sample(fields[k], out);
      const s = fields[k];
      for (const [dep, vals] of Object.entries(s.requiredWith || {})) {
        if (vals.includes(out[dep]) && out[k] === undefined) out[k] = sample(s, out);
      }
    }
    return out;
  };

  const cases = [];
  const plan = (actions) => ({ version: 1, reply: 'ok', actions });
  const add = (c) => cases.push(c);

  // ── per op ────────────────────────────────────────────────────────────────
  for (const e of registry.ops) {
    const keys = e.keys;
    const profiles = e.profiles;
    // Invalid-param cases: a surface of the profile that does not register the op sees an
    // unknown-op skip instead (plan valid) — recorded as knownDivergence.
    const skipSurfaces = e.surfaces
      ? profiles.flatMap((p) => surfacesOf(p).filter((s) => !e.surfaces.includes(s))) : [];
    const invalid = (slug, action, reason, extra = {}) => {
      const c = { name: `gen-${e.id}-${slug}`, generated: true, profiles, input: plan([{ op: e.name, ...action }]), expect: 'invalid', reason, ...extra };
      if (skipSurfaces.length) c.knownDivergence = Object.fromEntries(skipSurfaces.map((s) => [s, 'valid']));
      add(c);
    };
    const valid = (slug, action, reason) =>
      add({ name: `gen-${e.id}-${slug}`, generated: true, profiles, input: plan([{ op: e.name, ...action }]), expect: 'valid', reason });
    const base = minimalObject(keys, e);
    // A key present alongside whatever it needs (its form, its onlyWith dependency).
    const withKey = (k, v) => {
      const o = minimalObject(keys, e, [k]);
      o[k] = v;
      return o;
    };

    valid('minimal', base, 'the smallest action the registry admits');
    invalid('unknown-field', { ...base, zzzCanary: 1 }, 'an undeclared field on a known op fails the plan (§1)');
    if (e.forms) {
      invalid('forms-none', Object.fromEntries(Object.entries(base).filter(([k]) => !e.forms.flat().includes(k))), `exactly one of ${e.forms.map((f) => f.join('+')).join(' / ')} is required — none given`);
      if (e.forms.length > 1) {
        const both = { ...minimalObject(keys, e, e.forms[0]), ...minimalObject(keys, e, e.forms[1]) };
        invalid('forms-both', both, `exactly one of ${e.forms.map((f) => f.join('+')).join(' / ')} is required — two given`);
      }
    }
    for (const group of e.together || []) {
      const o = minimalObject(keys, e, group); delete o[group[1]];
      invalid(`together-${group[0]}-alone`, o, `${group.join(' and ')} ride together`);
    }
    if (e.minFields) invalid('no-fields', {}, `needs at least ${e.minFields} of ${Object.keys(keys).join('/')}`);

    for (const [k, s] of Object.entries(keys)) {
      if (s.required && !e.forms) {
        const o = { ...base }; delete o[k];
        invalid(`${k}-missing`, o, `"${k}" is required`);
      }
      invalid(`${k}-wrong-type`, withKey(k, s.type === 'object' ? 5 : {}), `"${k}" must be a ${s.type}`);
      if (s.enum && s.type === 'string') invalid(`${k}-enum-bogus`, withKey(k, 'zzz'), `"${k}" must be one of ${s.enum.join(', ')}`);
      if (s.enum && s.type === 'boolean') invalid(`${k}-not-true`, withKey(k, !s.enum[0]), `"${k}" must be ${s.enum[0]}`);
      if (s.range) {
        const [lo, hi] = s.range;
        const noun = s.type === 'integer' ? 'an integer' : 'a number';
        if (lo != null) {
          valid(`${k}-min-ok`, withKey(k, lo), `"${k}" at its lower bound ${lo}`);
          invalid(`${k}-below-min`, withKey(k, lo - 1), `"${k}" must be ${noun} >= ${lo}`);
        }
        if (hi != null) {
          valid(`${k}-max-ok`, withKey(k, hi), `"${k}" at its upper bound ${hi}`);
          invalid(`${k}-above-max`, withKey(k, hi + 1), `"${k}" must be ${noun} <= ${hi}`);
        }
      }
      if (s.type === 'integer') invalid(`${k}-fractional`, withKey(k, (s.range?.[0] ?? 1) + 0.5), `"${k}" must be an integer`);
      const hasGrammar = s.regex || s.regexBy;
      if (s.type === 'string' && s.maxChars != null && !hasGrammar && !s.enum) {
        const n = limit(s.maxChars);
        // A path-like key (regexNot URL_SCHEME) keeps a file extension so the surfaces
        // with an openable-file post-check (desktop, cli) still accept the cap case.
        const fill = (len) => (s.regexNot ? `${'x'.repeat(len - 4)}.png` : 'x'.repeat(len));
        valid(`${k}-maxchars-ok`, withKey(k, fill(n)), `"${k}" at its ${n}-character cap`);
        invalid(`${k}-maxchars-over`, withKey(k, fill(n + 1)), `"${k}" is longer than ${n} characters`);
      }
      if (s.nonEmpty) invalid(`${k}-blank`, withKey(k, '   '), `"${k}" must be a non-empty string`);
      if (hasGrammar) {
        const names = s.regexBy ? Object.values(s.regexBy.map) : [].concat(s.regex);
        invalid(`${k}-grammar-bogus`, withKey(k, names.includes('CSS_NAME') ? '12 34' : 'zzz'), `"${k}" must match ${names.join(' or ')}`);
      }
      if (s.regexNot) invalid(`${k}-url-rejected`, withKey(k, 'https://example.com/x'), `"${k}" must not be a URL`);
      if (s.type === 'array' && s.items && !s.opset) {
        const min = s.minItems != null ? limit(s.minItems) : null;
        const max = s.maxItems != null ? limit(s.maxItems) : null;
        const item = () => sample(s.items, {});
        if (min != null) invalid(`${k}-below-min-items`, withKey(k, Array.from({ length: min - 1 }, item)), `"${k}" needs at least ${min} entries`);
        if (max != null) {
          valid(`${k}-max-items-ok`, withKey(k, Array.from({ length: max }, item)), `"${k}" at its ${max}-entry cap`);
          invalid(`${k}-above-max-items`, withKey(k, Array.from({ length: max + 1 }, item)), `more than ${max} entries in "${k}"`);
        }
      }
      for (const [dep, vals] of Object.entries(s.onlyWith || {})) {
        const other = keys[dep].enum.find((v) => !vals.includes(v));
        if (other !== undefined) {
          const o = withKey(k, sample(s, {})); o[dep] = other;
          invalid(`${k}-without-${dep}`, o, `"${k}" only applies with "${dep}" ${vals.join('/')}`);
        }
      }
      for (const [dep, vals] of Object.entries(s.requiredWith || {})) {
        const o = { ...base, [dep]: vals[0] }; delete o[k];
        invalid(`${k}-required-with-${dep}`, o, `"${k}" is required with "${dep}" ${vals[0]}`);
      }
    }
  }


  cardCases(registry, limit, add);
  // Names must be unique — a collision means two rules produced the same slug.
  const seen = new Set();
  for (const c of cases) { if (seen.has(c.name)) throw new Error(`duplicate generated fixture ${c.name}`); seen.add(c.name); }
  return cases;
};
