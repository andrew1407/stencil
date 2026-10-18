// CLI mode as the facade sees it: while it is on, a window, the chat, fullscreen, the tooltip
// and every animation or look setting refuse from this one place, so a script cannot open what
// the terminal is covering. Desktop twin: the same list in app/console/ConsoleHost.cpp.
import { publish, EVENTS } from '../../eventBus/appBus.js';

let on = false;

export const cliModeOn = () => on;

export const setCliMode = (next) => {
  if (on === !!next) return on;
  on = !!next;
  publish(EVENTS.cliModeChanged, on);
  return on;
};

export const cliModeGate = (what) => {
  if (on) throw new Error(`stencil: CLI mode is on — ${what} waits until /clioff`);
};

// The settings keys the gate covers: animation, windows, the look and the voice input.
export const GATED_SETTINGS = Object.freeze([
  'motionMode', 'motionSpeed', 'consoleSpeed', 'drawingAnimations', 'multiWindow', 'notifyChannel',
  'selectionGlow', 'hoverRing', 'focusRing', 'fillColor', 'voiceInputLanguage', 'voiceSilenceMs',
]);
