// The extension pages' accent: applied before first paint, and the window.StencilAccent
// facade the options page and the logo gestures drive it through.
//
// This file is the sixth of SEVEN classic <script>s (MV3 forbids inline page scripts)
// loaded in each extension page's <head> BEFORE lib/theme.css, so the saved accent sits
// on <html data-accent="…"> before first paint — a module would be deferred and flash.
// In order: prefs.js, swapGeometry.js, dustGrains.js, dustWake.js, themeSwap.js, this,
// shellPrefs.js. They share one page scope through window.StencilKit; each takes what it
// needs from it at the top and publishes what the next ones use at the bottom.
// --accent-2 shade and glows derive from --accent via color-mix() in lib/theme.css.
(function () {
  var K = window.StencilKit;
  var ACCENTS = K.ACCENTS, DEFAULT = K.DEFAULT, KEY = K.KEY, applyFavicon = K.applyFavicon, faviconSvg = K.faviconSvg;
  var has = K.has, hexOf = K.hexOf, mirror = K.mirror, needsDarkGlyph = K.needsDarkGlyph, onAccentInk = K.onAccentInk;
  var read = K.read, swap = K.swap, writePref = K.writePref;
  // The mirrored accent lets non-page contexts colour the on-page highlight to match
  // the theme (lib/highlightColor.js).
  // ── Custom accent + hover preview (browser parity: accentController) ──
  // A double-clicked custom accent is PAGE-ONLY: an inline --accent override, gone on
  // reload. A hover preview paints a preset and reverts on leave. Neither persists.
  var normalizeHex = function (v) {
    var m = /^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.exec(String(v || '').trim());
    if (!m) return null;
    var h = m[1];
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    return '#' + h.toLowerCase();
  };
  var applyAccentInk = function (hex) {
    if (needsDarkGlyph(hex)) document.documentElement.setAttribute('data-accent-light', '');
    else document.documentElement.removeAttribute('data-accent-light');
  };
  var setFaviconHex = function (hex) {
    if (typeof document === 'undefined' || !document.head) return;
    var link = document.querySelector('link[rel="icon"]');
    if (!link) { link = document.createElement('link'); link.rel = 'icon'; document.head.appendChild(link); }
    link.type = 'image/svg+xml';
    link.href = 'data:image/svg+xml,' + encodeURIComponent(faviconSvg(hex));
  };
  var previewSnap = null;   // {data, inline} captured on the first hover of an open menu
  // Tell same-page listeners (the options accent dropdown) that the accent moved — the
  // logo's own gestures change it out from under them. Browser twin: accentController.announce.
  var announce = function (v) {
    try { window.dispatchEvent(new CustomEvent('stencil:accent-changed', { detail: v })); } catch (e) { /* no DOM */ }
  };

  var apply = function (k) {
    var next = has(k) ? k : DEFAULT;
    previewSnap = null;   // a committed change supersedes any hover preview
    document.documentElement.style.removeProperty('--accent');   // drop any custom override
    document.documentElement.setAttribute('data-accent', next);
    // A light accent flips every on-accent label and glyph to the dark ink (lib/theme.css).
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
    // The ink for a swatch that paints its own chip (the ✓ on a preset row), which
    // no --on-accent var can reach.
    inkOn: onAccentInk,
    // `from` is the control that was pressed (element or id) — the options page has no
    // #theme-toggle, so without it the wipe would have to guess.
    set: function (k, from) {
      var next = has(k) ? k : DEFAULT;
      // NOW, not inside the swap: `apply` clears it too, but the transition runs that
      // callback a beat later and the menu's own close ends the preview in between, so a
      // snapshot still standing there floods the page back to the previous accent.
      // Browser twin: applyAccent.
      previewSnap = null;
      writePref(KEY, next);
      swap(function () { apply(next); }, from || 'theme-toggle');
      announce(next);
      return next;
    },
    // Page-only custom accent (the logo's double-click picker): an inline --accent that
    // overrides the data-accent preset rule; NOT persisted, gone on reload. Returns the
    // normalized '#rrggbb', or null for junk. Browser twin: accentController.setCustomAccent.
    setCustom: function (hex, from) {
      var norm = normalizeHex(hex);
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
    // Hover preview: paint a preset while the pointer rests on its row, WITH the same
    // flood-from-the-control a real change plays (swap) — but no persist; endAccentPreview()
    // floods back to the committed accent (preset or custom). The first call of an open
    // menu snapshots what is committed. `from` is the control the wipe blooms from.
    previewAccent: function (key, from) {
      if (!has(key)) return;
      var el = document.documentElement;
      if (!previewSnap) previewSnap = { data: el.getAttribute('data-accent'),
                                        inline: el.style.getPropertyValue('--accent') };
      swap(function () {
        el.style.removeProperty('--accent');
        el.setAttribute('data-accent', key);
        applyAccentInk(hexOf(key));
      }, from || 'theme-toggle');
    },
    endAccentPreview: function (from) {
      if (!previewSnap) return;
      var el = document.documentElement;
      var snap = previewSnap; previewSnap = null;
      swap(function () {
        if (snap.inline) el.style.setProperty('--accent', snap.inline); else el.style.removeProperty('--accent');
        if (snap.data) el.setAttribute('data-accent', snap.data); else el.removeAttribute('data-accent');
        applyAccentInk(snap.inline || hexOf(snap.data || DEFAULT));
      }, from || 'theme-toggle');
    },
  };

  // Published for the scripts after this one (see accent.js).
  K.apply = apply;
})();
