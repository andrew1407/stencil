// Shared rig for the ctx-assistant.test.js family: one layout() composition against a
// stubbed localStorage, since the Assistant markup is gated on the saved LLM settings.
import { layout } from '../../js/ui/layout.js';

// layout() reads the saved LLM settings each call — swap them for one composition.
export const layoutWith = (settings) => {
  const saved = globalThis.localStorage;
  globalThis.localStorage = {
    getItem: (k) => (k === 'drawingApp_llmSettings' ? JSON.stringify(settings) : null),
    setItem: () => {},
  };
  try { return layout(); } finally {
    if (saved === undefined) delete globalThis.localStorage; else globalThis.localStorage = saved;
  }
};
