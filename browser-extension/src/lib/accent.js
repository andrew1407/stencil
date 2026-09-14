// The extension pages' accent, applied before first paint, plus the window.StencilAccent facade.
// Sixth of SEVEN classic <script>s (MV3 forbids inline page scripts; a module would defer and
// flash) loaded in each page's <head> BEFORE lib/theme/, sharing one scope through
// window.StencilKit — in order: prefs, swapGeometry, dustGrains, dustWake, themeSwap, this, shellPrefs.
(function () {
  const K = window.StencilKit;
  const ACCENTS = K.ACCENTS, DEFAULT = K.DEFAULT, KEY = K.KEY, applyFavicon = K.applyFavicon, faviconSvg = K.faviconSvg;
  const has = K.has, hexOf = K.hexOf, mirror = K.mirror, needsDarkGlyph = K.needsDarkGlyph, onAccentInk = K.onAccentInk;
  const read = K.read, swap = K.swap, writePref = K.writePref;
  // A double-clicked custom accent is PAGE-ONLY (an inline --accent, gone on reload); a hover
  // preview reverts on leave. Neither persists. Browser twin: accentController.
  const normalizeHex = function (v) {
    const m = /^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.exec(String(v || '').trim());
    if (!m) return null;
    let h = m[1];
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    return '#' + h.toLowerCase();
  };
  const applyAccentInk = function (hex) {
    if (needsDarkGlyph(hex)) document.documentElement.setAttribute('data-accent-light', '');
    else document.documentElement.removeAttribute('data-accent-light');
  };
  const setFaviconHex = function (hex) {
    if (typeof document === 'undefined' || !document.head) return;
    let link = document.querySelector('link[rel="icon"]');
    if (!link) { link = document.createElement('link'); link.rel = 'icon'; document.head.appendChild(link); }
    link.type = 'image/svg+xml';
    link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hex));
  };
  let previewSnap = null;   // {data, inline} captured on the first hover of an open menu
  // Same-page listeners (the options accent dropdown) learn the logo's gestures moved the accent.
  const announce = function (v) {
    try { window.dispatchEvent(new CustomEvent('stencil:accent-changed', { detail: v })); } catch (e) { /* no DOM */ }
  };

  const apply = function (k) {
    const next = has(k) ? k : DEFAULT;
    previewSnap = null;   // a committed change supersedes any hover preview
    document.documentElement.style.removeProperty('--accent');   // drop any custom override
    document.documentElement.setAttribute('data-accent', next);
    // A light accent flips every on-accent label and glyph to the dark ink (lib/theme/palette.css).
    applyAccentInk(hexOf(next));
    applyFavicon(k);
    mirror({ stencil_accent: next });
  };
  apply(read());
  window.StencilAccent = {
    list: ACCENTS,
    storageKey: KEY,
    get: read,
    hexOf: hexOf,
    inkOn: onAccentInk,
    // `from` is the control that was pressed (element or id) — the options page has no
    // #theme-toggle, so without it the wipe would have to guess.
    set: function (k, from) {
      const next = has(k) ? k : DEFAULT;
      // Cleared NOW, not inside the swap: the menu's close ends the preview before the
      // deferred apply runs, and a standing snapshot would flood back the previous accent.
      previewSnap = null;
      writePref(KEY, next);
      swap(function () { apply(next); }, from || 'theme-toggle');
      announce(next);
      return next;
    },
    // Page-only custom accent: an inline --accent, NOT persisted. Returns '#rrggbb' or null.
    setCustom: function (hex, from) {
      const norm = normalizeHex(hex);
      if (!norm) return null;
      previewSnap = null;   // a commit supersedes any hover preview
      swap(function () {
        document.documentElement.style.setProperty('--accent', norm);
        applyAccentInk(norm);
        setFaviconHex(norm);
      }, from || 'theme-toggle');
      announce(norm);
      return norm;
    },
    // Hover preview: the same flood as a real change, but no persist; the first call of an
    // open menu snapshots what is committed and endAccentPreview() floods back to it.
    previewAccent: function (key, from) {
      if (!has(key)) return;
      const el = document.documentElement;
      previewSnap ??= { data: el.getAttribute('data-accent'),
                        inline: el.style.getPropertyValue('--accent') };
      swap(function () {
        el.style.removeProperty('--accent');
        el.setAttribute('data-accent', key);
        applyAccentInk(hexOf(key));
      }, from || 'theme-toggle');
    },
    endAccentPreview: function (from) {
      if (!previewSnap) return;
      const el = document.documentElement;
      const snap = previewSnap; previewSnap = null;
      swap(function () {
        if (snap.inline) el.style.setProperty('--accent', snap.inline); else el.style.removeProperty('--accent');
        if (snap.data) el.setAttribute('data-accent', snap.data); else el.removeAttribute('data-accent');
        applyAccentInk(snap.inline || hexOf(snap.data || DEFAULT));
      }, from || 'theme-toggle');
    },
  };

  // Published for the scripts after this one.
  K.apply = apply;
})();
