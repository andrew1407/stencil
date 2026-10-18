// The CLI logo's accent actions, ports of cli/src/console/hooks.zig: a click advances the preset
// (a custom colour goes back to the default), a double-click lands on a random vivid custom colour.
import { ACCENTS, DEFAULT_ACCENT } from '../../core/settings/accents.js';

export const cycleAccent = (app, origin = null) => {
  if (app.customAccent) return app.accents.setAccent(DEFAULT_ACCENT, origin);
  const keys = ACCENTS.map((a) => a.key);
  return app.accents.setAccent(keys[(keys.indexOf(app.accent) + 1) % keys.length], origin);
};

// HSV (hue, 0.7, 0.95): always vivid and readable, essentially never a preset (appearance.zig randomCustomTheme).
export const vividHex = (hue) => {
  const s = 0.7, v = 0.95, c = v * s, x = c * (1 - Math.abs(((hue / 60) % 2) - 1)), m = v - c;
  const [r, g, b] = hue < 60 ? [c, x, 0] : hue < 120 ? [x, c, 0] : hue < 180 ? [0, c, x] : hue < 240 ? [0, x, c] : hue < 300 ? [x, 0, c] : [c, 0, x];
  return `#${[r, g, b].map((q) => Math.round((q + m) * 255).toString(16).padStart(2, '0')).join('')}`;
};

export const randomVividAccent = (app, origin = null, random = Math.random) =>
  app.accents.setCustomAccent(vividHex(Math.floor(random() * 360)), origin);
