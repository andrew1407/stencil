// The webcore skin's live half: while <html data-skin="webcore"> (lib/prefs/shellPrefs.js) is
// stamped, every glyph wears pixel art, and a hold on the header mark turns the skin on or off.
// Browser twins: js/ui/webcore/toggle.js and the hold in js/ui/logo/stageTrigger.js.
import { swapIcons, pixelIconSvg } from './icons.js';
import { HOLD_MS, effectOf, resolveShow } from '../logo/stageRules.js';
import { pageApp, pageAccentHex } from '../logo/accents.js';
import { wirePressHold } from '../logo/hold.js';

export const isSkinOn = (doc = document) => doc.documentElement.getAttribute('data-skin') === 'webcore';

// Where the accent and the user's own motion mode resolve to the logoStage.json webcore row.
export const holdAllowed = (win = globalThis) => {
  const accent = win.StencilAccent, motion = win.StencilMotion;
  if (!accent || !motion || !win.StencilSkin) return false;
  const custom = win.document?.documentElement?.style?.getPropertyValue('--accent') || null;
  return effectOf(resolveShow(accent.get(), custom, motion.stored?.() ?? motion.get())) === 'webcore';
};

const setFavicon = (doc, svg) => {
  const link = doc.querySelector?.('link[rel="icon"]');
  if (link) link.href = `data:image/svg+xml,${encodeURIComponent(svg)}`;
};

// Repaints on every skin, theme or accent change, and swaps each glyph added while it is on.
export const installWebcore = (doc = document, win = globalThis) => {
  const root = doc.documentElement;
  let painted = '';
  const app = pageApp();
  const opts = () => ({ accent: pageAccentHex(app), dark: root.getAttribute('data-theme') === 'dark' });
  const paint = () => {
    const on = isSkinOn(doc), o = opts();
    const key = on ? `${o.accent}|${o.dark}` : '';
    if (key === painted) return;
    const wasOn = painted !== '';
    painted = key;
    // The frames and the hover ring wear the preset: the skin points --accent at its own navy.
    if (on && o.accent) root.style.setProperty('--wc-focus', o.accent);
    else root.style.removeProperty('--wc-focus');
    if (on || wasOn) swapIcons(doc.body, on, o);
    const kit = win.StencilKit;
    if (on) setFavicon(doc, pixelIconSvg('logo', o, 16));
    else if (wasOn && kit && o.accent) setFavicon(doc, kit.faviconSvg(o.accent));
  };
  new win.MutationObserver(paint).observe(root,
    { attributes: true, attributeFilter: ['data-skin', 'data-theme', 'data-accent', 'style'] });
  new win.MutationObserver((records) => {
    if (!isSkinOn(doc)) return;
    const o = opts();
    for (const r of records) for (const n of r.addedNodes) if (n.nodeType === 1) swapIcons(n, true, o);
  }).observe(doc.body, { childList: true, subtree: true });
  paint();
};

export const wireWebcoreHold = (wrap, { holdMs = HOLD_MS, win = globalThis } = {}) =>
  wirePressHold(wrap, {
    holdMs,
    onHold: () => {
      if (!holdAllowed(win)) return false;
      win.StencilSkin.set(!win.StencilSkin.get());
      return true;
    },
  });
