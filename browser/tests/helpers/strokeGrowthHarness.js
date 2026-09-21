// Shared harness for the strokeGrowth specs: a clock and frame scheduler under the test's
// control, a counting renderer, and a line factory. No DOM, no timers.
import { StrokeFx } from '../../js/core/line/strokeFx.js';

export const harness = () => {
  let now = 0;
  const frames = [];
  const app = { pointSize: 4, redraws: 0, renderer: { redraw() { app.redraws++; } } };
  const fx = new StrokeFx(app, { now: () => now, schedule: (fn) => frames.push(fn) });
  return {
    app, fx, frames,
    at(t) { now = t; },
    frame() { const fns = frames.splice(0); fns.forEach((fn) => fn()); },
  };
};

export const lineOf = (...pts) => ({ points: pts.map(([x, y]) => ({ x, y })), color: '#f00', thickness: 3, pointSize: 4 });
