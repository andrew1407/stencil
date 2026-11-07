// The session skins' live half: one watch over <html data-skin>, the face and the accent, dressing the
// page in the skin stamped (lib/prefs/shellPrefs.js), undressing the one that left, and dressing what
// is added meanwhile. Browser twin: js/ui/skin/session.js.
import { makeWebcoreSkin } from '../webcore/skin.js';
import { makeAquaSkin } from '../aqua/skin.js';
import { faviconSvg, pageApp, pageAccentHex, pageDark } from '../logo/accents.js';
import { setFavicon } from './page.js';
import { skinOn } from './switch.js';

const MAKERS = Object.freeze({ webcore: makeWebcoreSkin, aqua: makeAquaSkin });
const installed = new WeakMap();   // document → dispose

// The look a skin paints in: the accent hex on screen and the face.
const readLook = (root, app) => ({ accent: pageAccentHex(app), dark: pageDark(root) });
const keyOf = (name, look) => (name ? `${name}|${look.accent}|${look.dark}` : '');

// Watches `doc` for the session; a second install first disposes the first. Returns the disposer.
export const installSkins = (doc = document, win = globalThis) => {
  installed.get(doc)?.();
  const root = doc.documentElement;
  const app = pageApp();
  const skins = Object.fromEntries(Object.entries(MAKERS).map(([name, make]) => [name, make(doc, win)]));
  let painted = { key: '', name: null, dark: false };
  const paint = () => {
    const name = skinOn(doc);
    let look = readLook(root, app);
    if (keyOf(name, look) === painted.key) return;
    const prev = painted;
    if (prev.name && prev.name !== name) {
      skins[prev.name].shed();
      skins[prev.name].leave?.();
      look = readLook(root, app);
    }
    if (name && name !== prev.name) {
      skins[name].enter?.();
      look = readLook(root, app);
      skins[name].wear(look);
    } else if (name && look.dark !== prev.dark) skins[name].wear(look);
    else if (name) skins[name].tint(look);
    painted = { key: keyOf(name, look), name, dark: look.dark };
    bodyWatch.takeRecords?.();   // the skin's own glyph writes are not additions to dress
    if (name || prev.name) setFavicon(doc, faviconSvg(look.accent));
  };
  const rootWatch = new win.MutationObserver(paint);
  const bodyWatch = new win.MutationObserver((records) => {
    if (!painted.name) return;
    const look = readLook(root, app);
    for (const r of records) for (const n of r.addedNodes) if (n.nodeType === 1) skins[painted.name].dress(n, look);
  });
  rootWatch.observe(root, { attributes: true, attributeFilter: ['data-skin', 'data-theme', 'data-accent', 'style'] });
  bodyWatch.observe(doc.body, { childList: true, subtree: true });
  paint();
  const dispose = () => {
    rootWatch.disconnect();
    bodyWatch.disconnect();
    for (const skin of Object.values(skins)) skin.dispose?.();
    installed.delete(doc);
  };
  installed.set(doc, dispose);
  return dispose;
};
