// ── One verdict shape for every user-facing check ────────────────
// The tree grew three idioms for "is this acceptable": a boolean, a normalized-or-null,
// and a bare reason string — none of which can both gate a control AND say why. These
// wrap the existing primitives (the rules stay where they are) as one `{ok, reason}`.
import { isAccent, normalizeHex } from './accents.js';
import { normalizePageSize, parseLengthToken } from './units.js';
import { parseDuration } from './durationParser.js';
import { parseProjectFile } from './projectFile.js';
import { parseHotkey } from '../utils/keys.js';

// Frozen: callers pass it around and must not edit it. `reason` is '' when ok.
export const VALID = Object.freeze({ ok: true, reason: '' });
export const invalid = (reason) => ({ ok: false, reason });
const verdict = (pass, reason) => (pass ? VALID : invalid(reason));

// The name RULE lives in ProjectsStore (only it sees the registry); this is the door the
// UI knocks on, so no view reaches into the store for a validator of its own.
export const validateProjectName = (store, name, exceptId = null) =>
  store?.validateName ? store.validateName(name, exceptId) : VALID;

// `allowEmpty` is for the fields where '' is meaningful — a cleared project colour,
// points that follow the line colour.
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

// A duration like "7d"; 0 means keep forever.
export const validateDuration = (spec) =>
  verdict(parseDuration(spec) != null, `Invalid duration "${spec}"`);

// http(s) ONLY — the one scheme gate, so neither a poisoned store nor a scripted setter
// can aim a client at javascript:/file:/extension URLs.
export const HTTP_URL_RE = /^https?:\/\//i;
export const validateHttpUrl = (value) =>
  verdict(HTTP_URL_RE.test(String(value ?? '')), `"${value}" must be an http(s) URL`);

export const validateHotkey = (combo) =>
  verdict(!!parseHotkey(combo), `"${combo}" is not a shortcut — try something like Ctrl+Shift+K`);

// The engine owns the grammar (and is wasm-bound), so it comes in rather than being
// imported: the caller already holds the app's one engine. Blank is valid (identity).
export const validateFormula = (engine, expr, axis = 'x') => {
  const a = axis === 'y' ? 'y' : 'x';
  const v = String(expr ?? '').trim();
  return verdict(!v || !!engine?.validate(v, a), `Invalid ${a} formula: ${expr}`);
};

// parseProjectFile answers {ok, project} / {ok, error} — the same verdict in the shared
// vocabulary, with the parsed project alongside.
export const validateProjectFileText = (text) => {
  const res = parseProjectFile(text);
  return res.ok ? { ...VALID, project: res.project } : invalid(res.error);
};
