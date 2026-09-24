// The webcore toggle (ui/logo/stageTrigger.js dispatches here): a session-only skin over the
// whole page — light, still, pixel-drawn — and its undoing. Nothing here writes a store.
// Desktop twin: MainWindow::toggleWebcore (app/logo/MainWindowWebcore.cpp).
import { accentHex, applyAccentFavicon, applyFaviconHex, normalizeHex, setFaviconArt } from '../../core/settings/accents.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { SKIN_ATTR, SKIN_NAME } from './rules.js';
import { setPixelIcons, swapIconArt, paintLogoMark, pixelIconSvg } from './icons.js';
import { createWebcoreScene, openWebcoreProject } from './scene.js';
import { setMotionOverride } from '../motion/motionPrefs.js';

export const webcoreActive = (doc = document) => doc.documentElement.getAttribute(SKIN_ATTR) === SKIN_NAME;

// The dark face has its own ink (config/iconsWebcore.json paletteDark).
const isDark = (doc) => doc.documentElement.getAttribute('data-theme') === 'dark';

// Every accent change repaints the favicon through this, so the tab mark follows the preset.
let skinDark = false;
const skinFavicon = (hex) => pixelIconSvg('logo', 16, hex, skinDark);

// accentChanged carries the preset key, or the hex itself when the colour is a custom one.
const markHex = (v) => normalizeHex(v) || accentHex(v);

// The mark's ring and the hover frame both wear the preset: --wc-focus is written here because
// the skin points --accent at its own navy.
const paintAccent = (root, hex) => root.style.setProperty('--wc-focus', hex);

const paintFavicon = (app) => {
  if (app.customAccent) applyFaviconHex(app.customAccent); else applyAccentFavicon(app.accent);
};

let unwatchAccent = null;
let unwatchTheme = null;

// Returns the state the skin is left in; `scene: false` never opens the webcore page.
export const toggleWebcore = async (app, doc = document, { scene = true } = {}) => {
  const root = doc.documentElement;
  if (webcoreActive(doc)) {
    root.removeAttribute(SKIN_ATTR);
    setPixelIcons(false);
    swapIconArt(doc.body, false);
    skinDark = false;
    unwatchAccent?.();
    unwatchTheme?.();
    unwatchAccent = unwatchTheme = null;
    setFaviconArt(null);
    root.style.removeProperty('--wc-focus');
    paintFavicon(app);
    setMotionOverride(null);   // the stored switches are back in force
    return false;
  }
  // The theme stays; line and interface animation stop for the session, never in the store, so
  // the Visuals rows can switch them back on and turning the skin off restores the stored ones.
  root.setAttribute(SKIN_ATTR, SKIN_NAME);
  setMotionOverride({ mode: 'none', drawing: false });
  const paintArt = () => {
    skinDark = isDark(doc);
    setPixelIcons(true, skinDark);
    swapIconArt(doc.body, true, markHex(app.customAccent || app.accent), skinDark);
  };
  paintArt();
  paintAccent(root, markHex(app.customAccent || app.accent));
  unwatchAccent ??= subscribe(EVENTS.accentChanged, (e) => {
    paintLogoMark(doc.body, markHex(e.detail), skinDark);
    paintAccent(root, markHex(e.detail));
  });
  unwatchTheme ??= subscribe(EVENTS.themeChanged, () => { paintArt(); paintFavicon(app); });
  setFaviconArt(skinFavicon);
  paintFavicon(app);
  if (scene && !app.image && !openWebcoreProject(app)) await createWebcoreScene(app);
  return true;
};
