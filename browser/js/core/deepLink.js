// ── Project deep links ──────────────────────────────────────────
// A saved project can open in a NEW browser tab from the projects modal. The new tab carries
// the target id in a query param (`?open=<id>`), which the booting app consumes once and
// strips from the URL. Pure string helpers (no DOM) for direct unit-testing.

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

// Build a URL that hands a full image off to a fresh tab via the `#stencil=<JSON>`
// fragment consumed by DrawingApp.applyExternalLaunch (shape
// { dataUrl, name?, incognito?, ... }). The payload rides in the fragment (not the
// query) so it stays off server logs and out of history after the receiver strips it.
// This is the only vehicle that works for incognito launches, since those are never
// persisted and so cannot be referenced by `?open=<id>`.
export const buildExternalLaunchUrl = (base, payload) =>
  `${base}#stencil=${encodeURIComponent(JSON.stringify(payload))}`;
