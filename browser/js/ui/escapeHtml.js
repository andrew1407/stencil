// ── The ONE HTML escaper ────────────────────────────────────────
// Every value interpolated into an innerHTML template goes through this — project
// names, server URLs/addresses, tooltip text. Non-strings coerce; null/undefined
// become ''. A security helper must exist once, so base.js and content.js (and
// the extension's port, src/lib/escapeHtml.js) re-export this one.
export const escapeHtml = (v) => String(v == null ? '' : v)
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;')
  .replace(/'/g, '&#39;');
