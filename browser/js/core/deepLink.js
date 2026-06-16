// ── Project deep links ──────────────────────────────────────────
// A saved project can be opened in a NEW browser tab from the projects modal.
// The new tab carries the target project id in a query param (`?open=<id>`),
// which the booting app consumes once and strips from the URL. Kept as pure
// string helpers (no DOM) so they can be unit-tested directly.

export const OPEN_PARAM = 'open';

// Read the requested project id from a location.search string (e.g.
// "?open=p_1&x=2"). Returns the id, or null when the param is absent/empty.
export const readOpenProjectId = (search = '') => {
  let params;
  try {
    params = new URLSearchParams(search || '');
  } catch {
    return null;
  }
  const id = params.get(OPEN_PARAM);
  return id ? id : null;
};

// Build the URL that opens `id` in a fresh tab. `base` is the page URL without a
// query string (e.g. location.origin + location.pathname); `id` is appended as
// the open param, URL-encoded.
export const buildOpenProjectUrl = (base, id) =>
  `${base}?${OPEN_PARAM}=${encodeURIComponent(id)}`;
