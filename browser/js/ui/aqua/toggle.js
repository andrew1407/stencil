// The aqua skin as a session (ui/skin/session.js runs it; ui/logo/stageTrigger.js dispatches here):
// gel in the chosen accent, sliding, glossy art, the Blue preset worn for the session, and the wallpaper
// an empty editor gets with its word. Desktop twin: ThemePainter::toggleSkin over AQUA_SHOW.
import { accentHex } from '../../core/settings/accents.js';
import { createSkinSession } from '../skin/session.js';
import { paintedBlob } from '../skin/scene.js';
import { swapGlyphs } from '../skin/glyphs.js';
import { AQUA, SKIN_NAME, OFF_TOAST, IMAGE_NAME, PROJECT_NAME, gelTokens, wordLines } from './rules.js';
import { swapAquaLogo, aquaFavicon } from './logo.js';
import { setAquaIcons } from './icons.js';
import { paintAquaImage } from './image.js';

// The preset the skin opens in, as webcore opens still: the user may pick another while it is on.
export const AQUA_ACCENT = 'blue';

const GEL_NAMES = Object.freeze(Object.keys(gelTokens(null)));
const paintGel = (doc, { hex, dark }) => {
  const root = doc.documentElement;
  for (const [name, value] of Object.entries(gelTokens(hex, dark))) root.style.setProperty(name, value);
  swapAquaLogo(doc.body, true, hex, dark);
};

// The accent the skin set aside for its session Blue; dropped once the user picks one of their own.
let ownAccent = null;
const wearBlue = (app) => {
  ownAccent = { key: app.accent, custom: app.customAccent };
  if (!ownAccent.custom && ownAccent.key === AQUA_ACCENT) return;
  app.accents?.setAccent?.(AQUA_ACCENT, null, { persist: false });
};
const giveBack = (app) => {
  const own = ownAccent;
  ownAccent = null;
  if (!own || (!own.custom && own.key === AQUA_ACCENT)) return;
  if (own.custom) app.accents?.setCustomAccent?.(own.custom);
  else app.accents?.setAccent?.(own.key, null, { persist: false });   // the store still holds it
};

export const aquaSkin = createSkinSession({
  name: SKIN_NAME,
  // Interface motion slides for the session, never in the store (desktop toggleSkin).
  motion: { mode: 'slide' },
  offToast: OFF_TOAST,
  favicon: aquaFavicon,
  scene: { imageName: IMAGE_NAME, projectName: PROJECT_NAME, image: AQUA.image, wordLines,
    blob: () => paintedBlob(AQUA.image, paintAquaImage, 'aqua') },
  // Blue for the session; the gel follows any accent picked after.
  wear: (app, doc, { dark }) => {
    const hex = accentHex(AQUA_ACCENT);
    paintGel(doc, { hex, dark });
    setAquaIcons(true);
    swapGlyphs(doc.body, true);
    wearBlue(app);
    return hex;
  },
  repaint: paintGel,
  onPick: () => { ownAccent = null; },   // the user's own pick stands when the skin goes
  shed: (app, doc) => {
    const root = doc.documentElement;
    for (const name of GEL_NAMES) root.style.removeProperty(name);
    setAquaIcons(false);
    swapGlyphs(doc.body, false);
    swapAquaLogo(doc.body, false);
    giveBack(app);
  },
});

export const aquaActive = aquaSkin.active;
export const toggleAqua = aquaSkin.toggle;
