// The webcore skin as a session (ui/skin/session.js runs it; ui/logo/stageTrigger.js dispatches here):
// light, still, pixel-drawn over the whole page, and the picture an empty editor gets with its word.
// Desktop twin: ThemePainter::toggleSkin over WEBCORE_SHOW.
import { createSkinSession } from '../skin/session.js';
import { paintedBlob } from '../skin/scene.js';
import { WEBCORE, wordLines, IMAGE_NAME, PROJECT_NAME, OFF_TOAST, SKIN_NAME } from './rules.js';
import { setPixelIcons, swapIconArt, paintLogoMark, pixelIconSvg } from './icons.js';
import { paintWebcoreImage } from './image.js';

// The mark's ring and the hover frame both wear the preset: --wc-focus is written here because
// the skin points --accent at its own navy.
const paintAccent = (doc, hex) => doc.documentElement.style.setProperty('--wc-focus', hex);

let skinDark = false;

export const webcoreSkin = createSkinSession({
  name: SKIN_NAME,
  // Still lines and a still interface for the session, never in the store (desktop toggleSkin).
  motion: { mode: 'none', drawing: false },
  offToast: OFF_TOAST,
  favicon: (hex) => pixelIconSvg('logo', 16, hex, skinDark),
  scene: { imageName: IMAGE_NAME, projectName: PROJECT_NAME, image: WEBCORE.image, wordLines,
    blob: () => paintedBlob(WEBCORE.image, paintWebcoreImage, 'webcore') },
  wear: (app, doc, { hex, dark }) => {
    skinDark = dark;
    setPixelIcons(true, dark);
    swapIconArt(doc.body, true, hex, dark);
    paintAccent(doc, hex);
  },
  repaint: (doc, { hex }) => { paintLogoMark(doc.body, hex, skinDark); paintAccent(doc, hex); },
  // The dark face has its own ink (config/iconsWebcore.json paletteDark): every glyph is re-inked.
  onTheme: (doc, { hex, dark }) => {
    skinDark = dark;
    setPixelIcons(true, dark);
    swapIconArt(doc.body, true, hex, dark);
  },
  shed: (app, doc) => {
    setPixelIcons(false);
    swapIconArt(doc.body, false);
    skinDark = false;
    doc.documentElement.style.removeProperty('--wc-focus');
  },
});

export const webcoreActive = webcoreSkin.active;
export const toggleWebcore = webcoreSkin.toggle;
