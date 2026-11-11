// The aqua skin as a session (ui/skin/session.js runs it; ui/logo/stageTrigger.js dispatches here):
// gel in the chosen accent, sliding, glossy art, the Blue preset its word stores, and the wallpaper
// an empty editor gets with its word. Desktop twin: ThemePainter::toggleSkin over AQUA_SHOW.
import { createSkinSession } from '../skin/session.js';
import { paintedBlob } from '../skin/scene.js';
import { swapGlyphs } from '../skin/glyphs.js';
import { AQUA, SKIN_NAME, OFF_TOAST, IMAGE_NAME, PROJECT_NAME, gelTokens, wordLines } from './rules.js';
import { swapAquaLogo, aquaFavicon } from './logo.js';
import { setAquaIcons } from './icons.js';
import { paintAquaImage } from './image.js';

// The preset the word sets, as webcore's sets Gray: the user may pick another while it is on.
export const AQUA_ACCENT = 'blue';

const GEL_NAMES = Object.freeze(Object.keys(gelTokens(null)));
const paintGel = (doc, { hex, dark }) => {
  const root = doc.documentElement;
  for (const [name, value] of Object.entries(gelTokens(hex, dark))) root.style.setProperty(name, value);
  swapAquaLogo(doc.body, true, hex, dark);
};

export const aquaSkin = createSkinSession({
  name: SKIN_NAME,
  // Sliding and Blue: the user's own once the word is typed (desktop toggleSkin).
  look: { motion: { mode: 'slide' }, accent: AQUA_ACCENT },
  offToast: OFF_TOAST,
  favicon: aquaFavicon,
  scene: { imageName: IMAGE_NAME, projectName: PROJECT_NAME, image: AQUA.image, wordLines,
    blob: () => paintedBlob(AQUA.image, paintAquaImage, 'aqua') },
  // The gel in the accent on screen, and in any picked after.
  wear: (app, doc, look) => {
    paintGel(doc, look);
    setAquaIcons(true);
    swapGlyphs(doc.body, true);
  },
  repaint: paintGel,
  shed: (app, doc) => {
    const root = doc.documentElement;
    for (const name of GEL_NAMES) root.style.removeProperty(name);
    setAquaIcons(false);
    swapGlyphs(doc.body, false);
    swapAquaLogo(doc.body, false);
  },
});

export const aquaActive = aquaSkin.active;
export const toggleAqua = aquaSkin.toggle;
