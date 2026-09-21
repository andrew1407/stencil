import {
  strokeBowSign, strokeFlyMs, strokeFlyPoint, strokePhase, strokeRipple,
  strokeSpark, strokeVertexScale, strokeWake,
} from '../../ui/motion.js';
import { drawMotionEnabled } from '../../ui/motion/motionPrefs.js';
import { pointColorOf } from '../draw/renderer.js';

// The vertices currently in flight (maths in ui/motion.js). A record holds the point
// OBJECT, not its index — a later insert shifts every index. Desktop twin: canvas/strokeGrowth.hpp.

const nowMs = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());
const TAU = Math.PI * 2;

export class StrokeFx {
  #app;
  #now;
  #schedule;
  #fx = [];        // { line, pt, from, to, bow, flyMs, start }
  #byPt = new Map();
  #suspended = false;
  #raf = 0;

  constructor(app, { now = nowMs, schedule = null } = {}) {
    this.#app = app;
    this.#now = now;
    // Wrapped, never stored bare: the host's own rAF would throw "Illegal invocation".
    this.#schedule = schedule
      || (typeof requestAnimationFrame === 'function' ? ((fn) => requestAnimationFrame(fn)) : null);
  }

  get active() { return this.#fx.length > 0; }

  // An export is the RESTING picture: nothing is drawn in flight while suspended.
  suspend() { this.#suspended = true; }
  resume() { this.#suspended = false; }

  // Asked per line and per point on every redraw, so the idle answer must cost nothing.
  get #idle() { return this.#suspended || this.#fx.length === 0; }

  #setFlights(fx) {
    this.#fx = fx;
    this.#byPt = new Map(fx.map((f) => [f.pt, f]));
  }

  // `from` defaults to the neighbour it hangs off; a line's first point has none and only pops.
  flyIn(line, idx, from = null) {
    // "Drawing animation" off puts the vertex straight where it was put.
    if (!this.#schedule || !drawMotionEnabled()) return null;
    const pts = line?.points;
    const pt = pts?.[idx];
    if (!pt) return null;
    const src = from || pts[idx - 1] || pts[idx + 1] || pt;
    const to = { x: pt.x, y: pt.y };
    const len = Math.hypot(to.x - src.x, to.y - src.y);
    // A rapid re-add replaces its own record rather than stacking two clocks on one point.
    const rec = {
      line, pt, to, from: { x: src.x, y: src.y },
      bow: strokeBowSign(to.x, to.y), flyMs: strokeFlyMs(len), start: this.#now(),
    };
    this.#setFlights([...this.#fx.filter((f) => f.pt !== pt), rec]);
    this.#kick();
    return rec;
  }

  // Each leaves the one before it and waits for it to land, so a rect draws edge by edge.
  flyInRange(line, startIdx, count, from = null) {
    let prev = from || (startIdx === 0 ? line.points[0] : null);
    let delay = 0;
    for (let i = 0; i < count; i++) {
      const rec = this.flyIn(line, startIdx + i, prev);
      if (!rec) return;
      rec.start += delay;
      delay += rec.flyMs * 0.55;
      prev = line.points[startIdx + i];
    }
  }

  cancel() { this.#setFlights([]); }

  has(line) { return !this.#idle && this.#fx.some((f) => f.line === line); }

  #recOf(pt) { return this.#idle ? null : this.#byPt.get(pt) || null; }

  // Each pass reads the clock ONCE and hands `t` down (desktop twin: canvasWidget's `const double now`).
  #phase(f, t) { return strokePhase(t - f.start, f.flyMs); }

  // The array itself when nothing on the line moves, so the common case allocates nothing.
  pointsOf(line) {
    if (!this.has(line)) return line.points;
    const t = this.#now();
    return line.points.map((p) => {
      const f = this.#recOf(p);
      if (!f) return p;
      const ph = this.#phase(f, t);
      return ph.fly >= 1 ? p : strokeFlyPoint(f.from, f.to, ph.fly, f.bow);
    });
  }

  scaleAt(pt) {
    const f = this.#recOf(pt);
    return f ? strokeVertexScale(this.#phase(f, this.#now())) : 1;
  }


  // The heat behind a flying vertex: a fat, faint stroke under the real one.
  paintUnder(ctx, line, pts) {
    if (!this.has(line)) return;
    const t = this.#now();
    line.points.forEach((p, i) => {
      const f = this.#recOf(p);
      if (!f) return;
      const a = strokeWake(this.#phase(f, t).span);
      if (a < 0.01) return;
      ctx.save();
      ctx.globalAlpha = a;
      ctx.strokeStyle = line.color;
      ctx.lineWidth = line.thickness + 7;
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      ctx.beginPath();
      const here = pts[i];
      for (const j of [i - 1, i + 1]) {
        if (!pts[j]) continue;
        ctx.moveTo(pts[j].x, pts[j].y);
        ctx.lineTo(here.x, here.y);
      }
      ctx.stroke();
      ctx.restore();
    });
  }

  // The glow riding the vertex, and the ring its landing pushes out.
  paintOver(ctx, line, pts) {
    if (!this.has(line)) return;
    const t = this.#now();
    const color = pointColorOf(line);
    const r = line.pointSize ?? this.#app?.pointSize ?? 4;
    line.points.forEach((p, i) => {
      const f = this.#recOf(p);
      if (!f) return;
      const ph = this.#phase(f, t);
      const at = pts[i];
      if (ph.fly < 1) {
        const sp = strokeSpark(ph.fly);
        ctx.save();
        ctx.globalAlpha = sp.alpha;
        ctx.fillStyle = color;
        ctx.shadowColor = color;
        ctx.shadowBlur = r * 2.5;
        ctx.beginPath();
        ctx.arc(at.x, at.y, r * sp.scale, 0, TAU);
        ctx.fill();
        ctx.restore();
      } else if (ph.ripple < 1) {
        const rp = strokeRipple(ph.ripple);
        ctx.save();
        ctx.globalAlpha = rp.alpha;
        ctx.strokeStyle = color;
        ctx.lineWidth = Math.max(1, r * 0.45 * (1 - ph.ripple));
        ctx.beginPath();
        ctx.arc(at.x, at.y, r * rp.scale, 0, TAU);
        ctx.stroke();
        ctx.restore();
      }
    });
  }

  // Once more after the last one lands, so the frame left on screen is the resting picture.
  #kick() {
    if (this.#raf || !this.#schedule) return;
    const step = () => {
      this.#raf = 0;
      const t = this.#now();
      this.#setFlights(this.#fx.filter((f) => !strokePhase(t - f.start, f.flyMs).done));
      this.#app?.renderer?.redraw();
      if (this.#fx.length) this.#raf = this.#schedule(step);
    };
    this.#raf = this.#schedule(step);
  }
}
