// One `{ok, reason}` verdict shape over the existing primitives (the rules stay where they
// are), so a check can both gate a control AND say why.
import { normalizeHex } from '../settings/accents.js';

// Frozen: callers pass it around and must not edit it.
const VALID = Object.freeze({ ok: true, reason: '' });
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

// http(s) ONLY — the one scheme gate against javascript:/file:/extension URLs.
const HTTP_URL_RE = /^https?:\/\//i;
export const validateHttpUrl = (value) =>
  verdict(HTTP_URL_RE.test(String(value ?? '')), `"${value}" must be an http(s) URL`);
