// Wearing a skin on the CLI stage: the cell colours go on the stage as CSS variables, the
// column hues and letter pictures repaint through the banner, an animated skin runs its frame
// clock, the blue screen prints its screen and the fairy lights recolour the accent, bulb by bulb.
import { traitsOf, FRAME_MS, createSkinClock, GOLD } from './table.js';
import { restyles, accentUnder, hueGradient, HUE_PERIOD } from './restyle.js';
import { rain, gridOf } from './rain.js';

const VARS = ['--cli-fg', '--cli-bg', '--cli-title-fg', '--cli-title-bg', '--cli-wash', '--cli-panel', '--cli-frame', '--accent', '--cli-hues', '--cli-slide'];
const FLAGS = ['data-cli-skin', 'data-cli-restyled', 'data-cli-runs', 'data-cli-cells'];

// The skin as the stylesheet reads it (cli.css): its colours as variables, its ways as flags.
export const applySkinVars = (stage, name) => {
  const t = traitsOf(name);
  for (const v of VARS) stage.style.removeProperty(v);
  for (const f of FLAGS) stage.removeAttribute(f);
  if (!t) return;
  stage.setAttribute('data-cli-skin', name);
  if (restyles(name)) stage.setAttribute('data-cli-restyled', '');
  if (t.paintsCells) {
    stage.setAttribute('data-cli-cells', '');
    stage.style.setProperty('--cli-fg', t.fg);
    stage.style.setProperty('--cli-bg', t.bg);
    stage.style.setProperty('--cli-title-fg', t.titleFg);
    stage.style.setProperty('--cli-title-bg', t.titleBg);
    stage.style.setProperty('--cli-panel', t.bg);   // every cell wears the skin's ground, the panel too
    stage.style.setProperty('--cli-frame', t.bg);
  }
  // The accent itself turns under a cell painter or a picture skin: every run wearing it follows.
  if (t.paintsCells || t.replacesLetters) stage.style.setProperty('--accent', accentUnder(name, ''));
  if (t.paintsRuns) {
    stage.setAttribute('data-cli-runs', '');
    stage.style.setProperty('--cli-hues', hueGradient(name));
  }
  if (t.wash) stage.style.setProperty('--cli-wash', t.wash);
};

// The whole dressing of one skin: `off` takes it off, `cut` ends its entrance at once (a key);
// `host` paints the banner, prints, recolours the accent and owns the scrollback (`out`).
export const wearSkin = (stage, name, host, { now = () => performance.now(), schedule = (fn, ms) => setTimeout(fn, ms), cancel = clearTimeout, random = Math.random } = {}) => {
  const t = traitsOf(name);
  applySkinVars(stage, name);
  if (!t) { host.repaint(0, null); return { off: () => {}, cut: () => {} }; }
  const clock = createSkinClock();
  clock.start(now(), Math.floor(now()) >>> 0);
  host.repaint(0, clock);
  if (t.phrase) host.print([t.phrase], 'secret');
  let stop = () => {};
  const grid = gridOf(host.out);
  if (name === 'matrix') stop = rain(host.out, { ...grid, random, schedule, onDone: () => host.repaintBody?.() });
  if (name === 'bluescreen') {   // the title bar centred over the columns, the body plain (messages.zig bluescreen)
    host.print([{ text: t.title, indent: Math.max(0, Math.floor((grid.cols - t.title.length) / 2)) }], 'title');
    host.print(['', ...t.body.split('\n')], 'plain');
  }
  let timer = null;
  const tick = () => {
    if (!clock.syncClock(now())) { timer = schedule(tick, FRAME_MS); return; }
    if (name === 'fairylight') { const bulb = clock.nextBulb(); if (bulb) host.setAccent(bulb); }
    else {
      if (t.slides) stage.style.setProperty('--cli-slide', `${clock.frame() % HUE_PERIOD}ch`);
      host.repaint(clock.frame(), clock);
    }
    timer = schedule(tick, FRAME_MS);
  };
  if (t.animated) timer = schedule(tick, FRAME_MS);
  const off = () => {
    stop();
    if (timer) cancel(timer);
    applySkinVars(stage, null);
    if (t.phraseOff) host.print([t.phraseOff], 'secret');
    host.repaint(0, null);
  };
  return { off, cut: () => stop() };
};

export { GOLD };
