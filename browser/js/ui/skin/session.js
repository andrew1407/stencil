// A session skin's life over the page (ui/logo/stageTrigger.js dispatches here): the <html data-skin>
// stamp, the motion it holds, the accent and theme watches that re-dress it, its favicon art and the
// scene an empty editor gets; a skin supplies only its painters. Desktop twin: ThemePainter::toggleSkin.
import { accentHex, normalizeHex, setFaviconArt } from '../../core/settings/accents.js';
import { applyAccentFavicon, applyFaviconHex } from '../accent/favicon.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { setMotionOverride } from '../motion/motionPrefs.js';
import { watchShownAccent } from '../accent/shownAccent.js';
import { whileIdleHeld } from '../control/state.js';
import { settleSkinScene } from './scene.js';

export const SKIN_ATTR = 'data-skin';

// The skin lifted off the page under the CLI stage, which looks the same under every skin.
let held = null;
export const activeSkin = (doc = document) => held ?? doc.documentElement.getAttribute(SKIN_ATTR);
export const holdSkin = (doc) => {
  const root = doc?.documentElement;
  held = root?.getAttribute?.(SKIN_ATTR) ?? null;
  if (held) root.removeAttribute(SKIN_ATTR);
  return held;
};
export const releaseSkin = (doc) => {
  const root = doc?.documentElement;
  if (held && root && !root.getAttribute(SKIN_ATTR)) root.setAttribute(SKIN_ATTR, held);
  held = null;
};

// accentChanged carries the preset key, or the hex itself when the colour is a custom one.
const markHex = (v) => normalizeHex(v) || accentHex(v);
const currentHex = (app) => markHex(app.customAccent || app.accent);
const isDark = (doc) => doc.documentElement.getAttribute('data-theme') === 'dark';
const paintFavicon = (app) => {
  if (app.customAccent) applyFaviconHex(app.customAccent); else applyAccentFavicon(app.accent);
};

// `skin`: { name, motion, offToast, favicon(hex), scene, wear(app, doc, look) → the hex it painted,
// repaint(doc, look), onTheme?(doc, look), onPick?(detail), shed(app, doc) }; `look` = { hex, dark }.
export const createSkinSession = (skin) => {
  let unwatch = [];
  const active = (doc = document) => activeSkin(doc) === skin.name;
  const off = (app, doc) => {
    doc.documentElement.removeAttribute(SKIN_ATTR);
    if (held === skin.name) held = null;
    for (const stop of unwatch) stop();
    unwatch = [];
    setFaviconArt(null);
    skin.shed(app, doc);
    paintFavicon(app);
    setMotionOverride(null);   // the stored switches are back in force
    return false;
  };
  const on = async (app, doc, openScene) => {
    doc.documentElement.setAttribute(SKIN_ATTR, skin.name);
    setMotionOverride(skin.motion);
    let painted = currentHex(app);
    const look = () => ({ hex: painted, dark: isDark(doc) });
    painted = skin.wear(app, doc, look()) ?? painted;
    unwatch = [
      subscribe(EVENTS.accentChanged, (e) => { skin.onPick?.(e.detail); painted = markHex(e.detail); skin.repaint(doc, look()); }),
      subscribe(EVENTS.themeChanged, () => { (skin.onTheme ?? skin.repaint)(doc, look()); paintFavicon(app); }),
      // A row the accent menu previews re-dresses the skin and the tab's mark too, as it floods the default face.
      watchShownAccent(doc, (hex) => {
        if (hex === painted) return;
        painted = hex;
        skin.repaint(doc, look());
        applyFaviconHex(hex);
      }),
    ];
    setFaviconArt(skin.favicon);
    paintFavicon(app);
    if (openScene && !app.image) await whileIdleHeld(() => settleSkinScene(app, skin.scene), doc);
    return true;
  };
  // Resolves to the state the skin is left in; `scene: false` never opens the skin's page.
  const toggle = (app, doc = document, { scene = true } = {}) => (active(doc) ? Promise.resolve(off(app, doc)) : on(app, doc, scene));
  return { name: skin.name, offToast: skin.offToast, active, toggle };
};
