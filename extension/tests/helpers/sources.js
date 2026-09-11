// Source-text assertions read whole surfaces, and three of them are split across files:
// the popup's stylesheet set (popup.html links them in this order), the assistant
// controller (popup/assistant.js plus the modules beside it) and the motion layer
// (lib/motionPrefs.js plus lib/motion/). Read each as one string.
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const read = (rel) => readFileSync(fileURLToPath(new URL(rel, import.meta.url)), 'utf8');

export const popupCss = () => ['popup.css', 'list.css', 'editorMode.css', 'chatPanel.css',
  'chatComposer.css', 'chatControls.css'].map((f) => read(`../../src/popup/${f}`)).join('\n');

const assistantDir = fileURLToPath(new URL('../../src/popup/assistant/', import.meta.url));
export const assistantSrc = () => [read('../../src/popup/assistant.js')]
  .concat(readdirSync(assistantDir).sort().map((f) => readFileSync(assistantDir + f, 'utf8'))).join('\n');

const motionDir = fileURLToPath(new URL('../../src/lib/motion/', import.meta.url));
export const motionSrc = () => [read('../../src/lib/motionPrefs.js')]
  .concat(readdirSync(motionDir).sort().map((f) => readFileSync(motionDir + f, 'utf8'))).join('\n');
