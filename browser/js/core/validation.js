// One `{ok, reason}` verdict shape over the existing primitives (the rules stay where they
// are), so a check can both gate a control AND say why.
import { isAccent, normalizeHex } from './accents.js';
import { normalizePageSize, parseLengthToken } from './units.js';
import { parseDuration } from './durationParser.js';
import { parseProjectFile } from './project/projectFile.js';
import { parseHotkey } from '../utils/keys.js';

// Frozen: callers pass it around and must not edit it.
export const VALID = Object.freeze({ ok: true, reason: '' });
export const invalid = (reason) => ({ ok: false, reason });
const verdict = (pass, reason) => (pass ? VALID : invalid(reason));

// The name RULE lives in ProjectsStore (only it sees the registry).
export const validateProjectName = (store, name, exceptId = null) =>
  store?.validateName ? store.validateName(name, exceptId) : VALID;

// `allowEmpty` is for the fields where '' is meaningful (a cleared colour, points that follow the line).
export const validateHexColor = (value, { allowEmpty = false } = {}) => {
  const v = String(value ?? '').trim();
  if (!v) return verdict(allowEmpty, 'Pick a colour');
  return verdict(!!normalizeHex(v), `Invalid color "${value}" — use a hex like #ff5623`);
};

export const validateAccent = (value) => {
  const v = String(value ?? '').trim();
  if (isAccent(v) || normalizeHex(v)) return VALID;
  return invalid(`Unknown accent "${value}" — use a hex like #ff5623, or a named accent`);
};

export const validatePageSize = (value) =>
  verdict(!!normalizePageSize(value), `Unknown page size "${value}"`);

export const validateLengthToken = (token) =>
  verdict(!!parseLengthToken(token), `Invalid length "${token}" — try 40, "40px", "2cm" or "25%"`);

export const validateDuration = (spec) =>
  verdict(parseDuration(spec) != null, `Invalid duration "${spec}"`);

// http(s) ONLY — the one scheme gate against javascript:/file:/extension URLs.
export const HTTP_URL_RE = /^https?:\/\//i;
export const validateHttpUrl = (value) =>
  verdict(HTTP_URL_RE.test(String(value ?? '')), `"${value}" must be an http(s) URL`);

export const validateHotkey = (combo) =>
  verdict(!!parseHotkey(combo), `"${combo}" is not a shortcut — try something like Ctrl+Shift+K`);

// The engine is wasm-bound and owned by the app, so it comes in. Blank is valid (identity).
export const validateFormula = (engine, expr, axis = 'x') => {
  const a = axis === 'y' ? 'y' : 'x';
  const v = String(expr ?? '').trim();
  return verdict(!v || !!engine?.validate(v, a), `Invalid ${a} formula: ${expr}`);
};

export const validateProjectFileText = (text) => {
  const res = parseProjectFile(text);
  return res.ok ? { ...VALID, project: res.project } : invalid(res.error);
};
