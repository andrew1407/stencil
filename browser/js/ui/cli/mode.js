// CLI mode: the terminal over the page. Entering it clears the way (every window, the chat,
// the logo show, fullscreen), sets the facade's gate and opens the stage; leaving releases
// both. The editor underneath is untouched — every edit made inside it is a real one.
import { notify } from '../../utils.js';
import { clearWay } from '../logo/stageTrigger.js';
import { closeOpenPopovers } from '../modal/registry.js';
import { TOAST_TEXT } from '../logo/stageRules.js';
import { setCliMode, cliModeOn } from '../../console/shell/gate.js';
import { openCliStage } from './stage.js';

export const OPEN_CLASS = 'cli-mode-open';
let stage = null;

export const cliModeActive = () => cliModeOn();

export const enterCliMode = (app, { origin = null, doc = globalThis.document } = {}) => {
  if (cliModeOn() || !doc?.body) return false;
  clearWay(app, doc);
  try { app.chat?.close?.(); } catch { /* no chat panel */ }
  closeOpenPopovers();
  doc.activeElement?.blur?.();
  setCliMode(true);
  doc.body.classList?.add?.(OPEN_CLASS);
  stage = openCliStage(app, { origin, doc, onLeave: () => exitCliMode(app, doc) });
  notify(TOAST_TEXT, 'ok', { shine: true });
  return true;
};

export const exitCliMode = (app, doc = globalThis.document) => {
  if (!cliModeOn()) return false;
  stage?.release();
  stage = null;
  doc?.body?.classList?.remove?.(OPEN_CLASS);
  setCliMode(false);
  return true;
};
