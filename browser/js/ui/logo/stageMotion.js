// The logo stage's kinematics, pure and clocked by the caller: the reveal tween, the bounce
// (shrink / grow), the mark that follows, flees or flies the cursor inside the window.
// Desktop twin: support/logoStageMotion.{hpp,cpp}, value for value.
import { easeLut } from '../dust/flight.js';
import { STAGE } from './stageRules.js';

const EASE_OUT = easeLut(0.22, 0.61, 0.36, 1);
const clamp01 = (v) => Math.max(0, Math.min(1, v));

// The reveal, at progress p: the header mark's centre and size to the stage's.
export const revealTween = (from, to, p) => {
  const e = EASE_OUT(clamp01(p));
  return { x: from.x + (to.x - from.x) * e, y: from.y + (to.y - from.y) * e, size: from.size + (to.size - from.size) * e };
};

// Keep a mark of `size` centred inside w×h.
export const clampCentre = (x, y, size, w, h) => ({
  x: Math.max(size / 2, Math.min(w - size / 2, x)),
  y: Math.max(size / 2, Math.min(h - size / 2, y)),
});

// ── Bounce: a click NUDGES the size a step toward `impulse`, and it eases back to `rest` ──
export const bounceState = (rest, impulse) => ({ size: rest, rest, impulse, phase: 'rest', from: rest, to: rest, t0: 0 });
// A resize re-measures the mark: every size the bounce carries scales with it, so a snap in
// flight keeps its shape instead of jumping to the new rest.
export const bounceResize = (st, rest, impulse) => {
  const k = st.rest > 0 ? rest / st.rest : 1;
  st.size *= k; st.from *= k; st.to *= k;
  st.rest = rest; st.impulse = impulse;
  return st;
};
// One click carries it a STEP of the way, not the whole way: the mark eases home faster than the
// steps land, so holding it out at either end takes real clicking.
export const bounceImpulse = (st, now, cfg = STAGE.bounce) => {
  const lo = Math.min(st.rest, st.impulse), hi = Math.max(st.rest, st.impulse);
  st.phase = 'snap';
  st.from = st.size;
  st.to = Math.min(hi, Math.max(lo, st.size + (st.impulse - st.rest) * cfg.stepShare));
  st.t0 = now;
  return st;
};
export const bounceStep = (st, now, cfg = STAGE.bounce) => {
  if (st.phase === 'rest') return st.size;
  // recoverMs is the time to cross the WHOLE range, so the way home runs at one rate however far
  // the clicks carried it — a short step unwinds in a short time.
  const range = Math.abs(st.impulse - st.rest) || 1;
  const span = st.phase === 'snap' ? cfg.snapMs
             : cfg.recoverMs * (Math.abs(st.to - st.from) / range);
  const p = span > 0 ? clamp01((now - st.t0) / span) : 1;
  st.size = st.from + (st.to - st.from) * EASE_OUT(p);
  if (p >= 1) {
    if (st.phase === 'snap') { st.phase = 'recover'; st.from = st.size; st.to = st.rest; st.t0 = now; }
    else st.phase = 'rest';
  }
  return st.size;
};

// ── Chase: the mark is thrown toward the cursor (or away from it) and DRAGS behind, so it
// never simply arrives — it overshoots, settles, and sets off again. Walls turn it around.
export const chaseState = (x, y) => ({ x, y, vx: 0, vy: 0 });

export const chaseStep = (st, cursor, dt, size, w, h, cfg, flee = false) => {
  const k = dt / 1000;
  const dx = cursor.x - st.x, dy = cursor.y - st.y;
  const dist = Math.hypot(dx, dy) || 1;
  // Fleeing only answers what is near: past the radius it coasts to a stop on drag alone.
  const pull = flee ? -cfg.stiffness * Math.max(0, 1 - dist / cfg.radiusPx) : cfg.stiffness;
  st.vx += (dx / dist) * pull * dist * k - st.vx * cfg.dragPerS * k;
  st.vy += (dy / dist) * pull * dist * k - st.vy * cfg.dragPerS * k;
  st.x += st.vx * k;
  st.y += st.vy * k;
  const half = size / 2;
  if (st.x < half) { st.x = half; st.vx = Math.abs(st.vx); }
  else if (st.x > w - half) { st.x = w - half; st.vx = -Math.abs(st.vx); }
  if (st.y < half) { st.y = half; st.vy = Math.abs(st.vy); }
  else if (st.y > h - half) { st.y = h - half; st.vy = -Math.abs(st.vy); }
  return st;
};

// The way it is travelling, as a unit vector — the cloud lays its tail the other way. Below
// the threshold it is not going anywhere, so it gets no tail and the grains ring it instead.
// The way the mark travels, its LENGTH carrying how strongly the tail forms: a mark creeping the
// last few pixels onto the cursor has to lay no tail, or its whole cloud sits a gap off-centre
// while it looks still. Null below the floor, full length at tailFullSpeedPx.
export const headingOfState = (st, cfg = STAGE.cloud) => {
  const mag = Math.hypot(st.vx, st.vy);
  if (!(mag > cfg.tailMinSpeedPx)) return null;
  const pull = Math.min(1, (mag - cfg.tailMinSpeedPx) / Math.max(1, cfg.tailFullSpeedPx - cfg.tailMinSpeedPx));
  return { x: (st.vx / mag) * pull, y: (st.vy / mag) * pull };
};

// ── Fly: a constant-speed drift that bounces off the walls; a punch adds speed that damps back ──
export const flyState = (x, y, angle, cfg = STAGE.fly) => ({
  x, y, vx: Math.cos(angle) * cfg.speedPx, vy: Math.sin(angle) * cfg.speedPx,
});
export const flyPunch = (st, angle, cfg = STAGE.fly) => {
  const v = cfg.speedPx + cfg.punchPx;
  st.vx = Math.cos(angle) * v; st.vy = Math.sin(angle) * v;
  return st;
};
export const flyStep = (st, dt, size, w, h, cfg = STAGE.fly) => {
  const k = dt / 1000;
  st.x += st.vx * k; st.y += st.vy * k;
  const half = size / 2;
  if (st.x < half) { st.x = half + (half - st.x); st.vx = Math.abs(st.vx); }
  else if (st.x > w - half) { st.x = (w - half) - (st.x - (w - half)); st.vx = -Math.abs(st.vx); }
  if (st.y < half) { st.y = half + (half - st.y); st.vy = Math.abs(st.vy); }
  else if (st.y > h - half) { st.y = (h - half) - (st.y - (h - half)); st.vy = -Math.abs(st.vy); }
  const c = clampCentre(st.x, st.y, size, w, h);
  st.x = c.x; st.y = c.y;
  const mag = Math.hypot(st.vx, st.vy);
  if (mag > cfg.speedPx && mag > 0) {
    const next = cfg.speedPx + (mag - cfg.speedPx) * Math.exp(-cfg.dampPerS * k);
    st.vx *= next / mag; st.vy *= next / mag;
  }
  return st;
};
