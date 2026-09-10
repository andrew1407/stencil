// ── The ONE HTML escaper ────────────────────────────────────────────────────
// Port of browser/js/ui/escapeHtml.js (portParity.test.js). Every value
// interpolated into an innerHTML template goes through this — scanned page titles,
// server addresses, tooltip text. Non-strings coerce; null/undefined become ''.
export const escapeHtml = (v) => String(v == null ? '' : v)
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;')
  .replace(/'/g, '&#39;');
