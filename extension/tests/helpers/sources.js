// Source-text assertions read whole surfaces, and several of them are split across files:
// the popup's and the shared stylesheet sets (each document links them in this order), the
// assistant controller (popup/assistant.js plus the modules beside it) and the motion layer
// (lib/motionPrefs.js plus lib/motion/). Read each as one string, so an assertion never
// has to know which sheet or module of a set now carries the thing it pins.
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const read = (rel) => readFileSync(fileURLToPath(new URL(rel, import.meta.url)), 'utf8');

export const popupCss = () => ['popup.css', 'list.css', 'editorMode.css', 'chatPanel.css',
  'chatComposer.css', 'chatControls.css'].map((f) => read(`../../src/popup/${f}`)).join('\n');

// The two shared sheets every extension document links, in that link order.
export const themeCss = () => ['palette.css', 'controls.css', 'tooltip.css', 'select.css',
  'fields.css'].map((f) => read(`../../src/lib/theme/${f}`)).join('\n');
export const animationsCss = () => ['keyframes.css', 'iconHover.css', 'controls.css',
  'themeSwap.css', 'reveal.css', 'pages.css', 'motionModes.css', 'reducedMotion.css',
  'chat.css', 'overlays.css', 'motionIcons.css']
  .map((f) => read(`../../src/lib/animations/${f}`)).join('\n');

// Every stylesheet one document links, concatenated in document order — the text the
// browser would actually cascade for that page.
export const docCss = (htmlRel) => {
  const htmlPath = fileURLToPath(new URL(`../../${htmlRel}`, import.meta.url));
  const html = readFileSync(htmlPath, 'utf8');
  const hrefs = [...html.matchAll(/<link\b[^>]*\brel\s*=\s*["']?\s*stylesheet\b[^>]*>/gi)]
    .map((m) => m[0].match(/href\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i))
    .map((h) => h && (h[1] ?? h[2] ?? h[3])).filter((u) => u && !/^(https?:)?\/\/|^data:/i.test(u));
  return hrefs.map((u) => readFileSync(resolve(dirname(htmlPath), u), 'utf8')).join('\n');
};

const assistantDir = fileURLToPath(new URL('../../src/popup/assistant/', import.meta.url));
export const assistantSrc = () => [read('../../src/popup/assistant.js')]
  .concat(readdirSync(assistantDir).sort().map((f) => readFileSync(assistantDir + f, 'utf8'))).join('\n');

const motionDir = fileURLToPath(new URL('../../src/lib/motion/', import.meta.url));
export const motionSrc = () => [read('../../src/lib/motionPrefs.js')]
  .concat(readdirSync(motionDir).sort().map((f) => readFileSync(motionDir + f, 'utf8'))).join('\n');
