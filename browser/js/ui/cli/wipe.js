// The accent recolour wipe, a port of cli/src/console/render/logoFx/press.zig wipeRecolor: the
// new accent jumps left to right over the header and the accented scrollback while the wordmark
// lights letter by letter and the icon turns as a clock. A keystroke settles it at once.
import FX from '../../../../common/config/console/fx.json' with { type: 'json' };
import { iconDegrees, iconSpans, ICON_COLS } from './clock.js';

const W = FX.wipe;

// The cells of `header` rows (spans wearing the accent) a frame at seam `x` paints new, as
// [row, c0, c1) over the row's columns; `reach` is the rightmost accented column.
export const wipeFrame = (headerRows, cols, x, reach) => {
  const deg = iconDegrees(x, reach);
  const out = [];
  for (let row = 1; row <= headerRows; row++) for (const [c0, c1] of iconSpans(row, headerRows, cols, deg, x)) out.push([row, c0, c1]);
  return out;
};

// How far the rule under the header has turned over at seam `x`: it runs ahead of the seam.
export const ruleReach = (cols, x, reach) => Math.min(cols, Math.floor((cols * x * W.ruleWipePct) / (Math.max(1, reach) * 100)));

// The schedule: wash jumps every stepMs, letters every flourishStepMs, both from one clock.
export const wipeSchedule = (reach, letters) => {
  const perJump = Math.max(1, Math.ceil(reach / W.jumps));
  const events = [];
  for (let x = perJump, t = 0; x < reach; x += perJump, t += W.stepMs) events.push({ at: t, kind: 'wash', x });
  for (let i = 0, t = 0; i < letters; i++, t += W.flourishStepMs) events.push({ at: t, kind: 'letter', i });
  return events.sort((a, b) => a.at - b.at || (a.kind === 'wash' ? -1 : 1));
};

// Runs the schedule through `paint`: `wash(x)` recolours up to seam x, `letter(i)` lights letter i,
// `settle()` ends it (every cell new, the wordmark whole). Returns a cancel that settles at once.
export const runWipe = ({ reach, letters, paint, schedule = (fn, ms) => setTimeout(fn, ms), cancelTimer = clearTimeout, instant = false }) => {
  const events = wipeSchedule(Math.max(ICON_COLS, reach), letters);
  if (instant || !events.length) { paint.settle(); return () => {}; }
  let i = 0, timer = null, done = false;
  const finish = () => { if (done) return; done = true; if (timer) cancelTimer(timer); paint.settle(); };
  const step = () => {
    const e = events[i++];
    if (e.kind === 'wash') paint.wash(e.x); else paint.letter(e.i);
    if (i >= events.length) { finish(); return; }
    timer = schedule(step, events[i].at - e.at);
  };
  timer = schedule(step, events[0].at);
  return finish;
};
