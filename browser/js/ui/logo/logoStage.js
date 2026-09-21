// The logo stage: one full-window canvas holding the big mark, its light and its cloud, and the
// lock that makes it the only thing the editor listens to until Escape or a click ends it.
// Desktop twin: app/LogoStage.{hpp,cpp}.
import { motionReduced, particleStyle } from '../motionPrefs.js';
import { modalShells } from '../modal/modalRegistry.js';
import { STAGE, effectOf, showStyle, bigLogoSize, bounceBigSize, minLogoSize, roams, roamLogoSize, markEdge } from './logoStageRules.js';
import { beatAt, spinAt, stageMark, markHex, paintBackdrop, paintGlow, paintSpokes, paintMark } from './logoStagePaint.js';
import { createStageCloud } from './logoStageCloud.js';
import * as M from './logoStageMotion.js';

export const STAGE_CLASS = 'logo-stage';
export const OPEN_CLASS = 'logo-stage-open';
let live = null;

export const logoStageOpen = () => !!live;

// Only from the bare window: no stage already up, not fullscreen, nothing modal on top.
export const logoStageAllowed = (doc = globalThis.document) => {
  if (live || typeof doc?.body?.appendChild !== 'function' || typeof doc.createElement !== 'function') return false;
  if (doc.body.classList?.contains('fullscreen-mode')) return false;
  for (const shell of modalShells) if (shell.isOpen()) return false;
  return !doc.querySelector?.('.app-modal-overlay.modal-open');
};

const SWALLOWED = ['keydown', 'keyup', 'keypress', 'wheel', 'contextmenu'];

export const closeLogoStage = () => {
  if (!live) return false;
  const stage = live;
  live = null;
  stage.release();
  return true;
};

// Where the header mark sits, so a show opened by a typed word or a facade call grows out of
// the logo exactly as a held one does.
export const markOrigin = (doc = globalThis.document) => {
  const r = doc?.querySelector?.('.app-logo-wrap')?.getBoundingClientRect?.();
  return r && (r.width || r.height) ? { x: r.left + r.width / 2, y: r.top + r.height / 2 } : null;
};

// `origin` is the header mark's centre, so the stage grows out of the logo and shrinks back into it.
export const openLogoStage = (name, { app, origin = null, doc = globalThis.document } = {}) => {
  const effect = effectOf(name);
  if (!effect || effect === 'pink' || !logoStageAllowed(doc)) return false;
  const reduced = motionReduced();
  const host = doc.createElement('div');
  host.className = STAGE_CLASS;
  host.setAttribute('aria-hidden', 'true');
  const canvas = doc.createElement('canvas');
  host.appendChild(canvas);
  doc.body.appendChild(host);
  // The show's own notice is the one thing that still stands above it (components/logoStage.css).
  doc.body.classList?.add?.(OPEN_CLASS);
  const ctx = canvas.getContext?.('2d') || null;
  const img = stageMark(doc, markHex(app));
  const style = reduced ? null : showStyle(name, particleStyle());
  const cloud = createStageCloud(style);

  const view = () => ({ w: globalThis.innerWidth || 1024, h: globalThis.innerHeight || 768 });
  let { w, h } = view();
  const roaming = roams(name);
  const bouncing = effect === 'shrink' || effect === 'grow';
  const big = () => (bouncing ? bounceBigSize(w, h) : bigLogoSize(w, h));
  const measure = () => (roaming ? roamLogoSize(w, h) : effect === 'grow' ? minLogoSize(w, h) : big());
  const measureOther = () => (effect === 'grow' ? big() : minLogoSize(w, h));
  let rest = measure(), other = measureOther();
  const bounce = M.bounceState(rest, other);
  const fly = M.flyState(w / 2, h / 2, Math.random() * 2 * Math.PI);
  const chase = M.chaseState(w / 2, h / 2);
  const pos = { x: w / 2, y: h / 2 };
  const cursor = { x: w / 2, y: h / 2 };
  let heading = null;   // the way it travels; the cloud lays its tail the other way
  const at = origin || markOrigin(doc);
  const from = at ? { x: at.x, y: at.y, size: 32 } : { x: w / 2, y: h / 2, size: rest };
  let size = rest, held = false, leftAt = null;
  // ONLY a press drives the light and the cloud: crossing the mark changes nothing, so the
  // loop never restarts under a passing pointer. The press EASES it in and out over its own
  // time constant — stepped to the target, the light and the grain count both jumped.
  let boost = 1;
  const rampBoost = (dt) => {
    const target = held ? STAGE.stage.holdBoost : 1;
    const k = reduced ? 1 : 1 - Math.exp(-dt / STAGE.stage.holdRampMs);
    boost += (target - boost) * k;
  };
  let t0 = 0, last = 0, frame = null, fade = reduced ? 1 : 0;

  const fit = () => {
    const was = { w, h };
    ({ w, h } = view());
    const dpr = Math.min(globalThis.devicePixelRatio || 1, 2);
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
    ctx?.setTransform(dpr, 0, 0, dpr, 0, 0);
    // The mark is measured FROM the window, so a resize re-measures it and carries everything
    // placed in the old box across as a fraction of it — otherwise the show keeps the size and
    // the centre of the window it opened in.
    const kx = was.w > 0 ? w / was.w : 1, ky = was.h > 0 ? h / was.h : 1;
    if (kx === 1 && ky === 1) return;
    rest = measure();
    other = measureOther();
    M.bounceResize(bounce, rest, other);
    size = effect === 'shrink' || effect === 'grow' ? bounce.size : rest;
    pos.x *= kx; pos.y *= ky;
    chase.x *= kx; chase.y *= ky;
    fly.x *= kx; fly.y *= ky;
  };
  fit();

  const drop = () => { host.remove(); doc.body.classList?.remove?.(OPEN_CLASS); };
  const now = () => (globalThis.performance?.now?.() ?? Date.now());
  const paint = (t) => {
    if (!ctx) return;
    const beat = reduced ? 0.5 : beatAt(t);
    // A cloud show's light is a steady lamp the grains fly through: all its motion, and
    // everything a hold adds, belongs to the cloud. Without a cloud the light does it all.
    const lightBoost = style ? 1 : boost;
    // Never all the way down: a neon sign breathes, it does not go out.
    const lit = style ? STAGE.glow.steadyLit : STAGE.glow.floor + (1 - STAGE.glow.floor) * beat;
    const reveal = reduced ? 1 : Math.min(1, t / STAGE.stage.revealMs);
    const p = leftAt === null ? reveal : 1 - Math.min(1, (t - leftAt) / STAGE.stage.hideMs);
    const pose = M.revealTween(from, { x: pos.x, y: pos.y, size }, p);
    paintBackdrop(ctx, w, h, fade);
    // The cloud wears the mark's own scale, so it grows out of the logo and shrinks back into it.
    cloud.draw(ctx, doc, pose.x, pose.y, t, size > 0 ? pose.size / size : 1);
    paintGlow(ctx, { ...pose, hex: markHex(app), beat: lit, boost: lightBoost });
    if (effect === 'sun') paintSpokes(ctx, { ...pose, hex: markHex(app), beat, boost: lightBoost, angle: reduced ? 0 : spinAt(t) });
    paintMark(ctx, img, pose);
  };

  const step = () => {
    frame = null;
    const t = now() - t0;
    const dt = Math.min(50, last ? t - last : 16.7);
    last = t;
    rampBoost(dt);
    fade = leftAt === null ? Math.min(1, t / STAGE.stage.revealMs)
      : Math.max(0, 1 - (t - leftAt) / STAGE.stage.hideMs);
    if (leftAt !== null && t - leftAt >= STAGE.stage.hideMs) { drop(); return; }
    if (!reduced) {
      if (effect === 'follow' || effect === 'escape') {
        M.chaseStep(chase, cursor, dt, size, w, h, STAGE[effect], effect === 'escape');
        pos.x = chase.x; pos.y = chase.y;
        heading = M.headingOfState(chase);
      } else if (effect === 'fly') {
        M.flyStep(fly, dt, size, w, h);
        pos.x = fly.x; pos.y = fly.y;
        // No tail: this one is not chasing anything, so its cloud stays a ring on every side.
        heading = null;
      }
      size = effect === 'shrink' || effect === 'grow' ? M.bounceStep(bounce, t) : size;
      // Spawning tapers with the fade, so the hide has nothing new arriving into it.
      cloud.step(dt, size, boost * fade, { dir: heading });
      syncCursor();
    }
    paint(t);
    frame = raf(step);
  };
  const raf = globalThis.requestAnimationFrame ?? ((fn) => setTimeout(() => fn(now()), 16));
  const cancel = globalThis.cancelAnimationFrame ?? clearTimeout;

  // The catch is the mark's OWN outline, not a circle round it: a circle reaches past the flat
  // edges and falls short of the corners, so the hand showed where the art was not. A fast mark
  // is past the pointer by the time a press lands, so it reaches ahead by grabLeadMs of travel.
  const onMark = (x, y) => {
    const dx = x - pos.x, dy = y - pos.y;
    const v = effect === 'fly' ? Math.hypot(fly.vx, fly.vy)
            : effect === 'follow' || effect === 'escape' ? Math.hypot(chase.vx, chase.vy) : 0;
    const edge = markEdge(size, Math.atan2(dy, dx));
    return Math.hypot(dx, dy) <= Math.hypot(edge.x, edge.y) + (v * STAGE.fly.grabLeadMs) / 1000;
  };
  // The mark is the one thing on the stage a press acts on, so it wears the hand — and a roaming
  // mark runs under a still pointer, so the frame re-asks as well as the move.
  let hand = null;
  const syncCursor = () => {
    const hot = onMark(cursor.x, cursor.y);
    if (hot !== hand) host.style.cursor = (hand = hot) ? 'pointer' : 'default';
  };
  const onPointerMove = (e) => {
    cursor.x = e.clientX ?? cursor.x;
    cursor.y = e.clientY ?? cursor.y;
    syncCursor();
  };
  // Off the mark, a press closes the stage. On it, a press is a HOLD: the light and the cloud
  // swell for as long as it lasts, and only the shows with a move of their own act on it.
  const onPointerDown = (e) => {
    e.preventDefault?.();
    if (!onMark(e.clientX ?? 0, e.clientY ?? 0)) { closeLogoStage(); return; }
    held = true;
    if (effect === 'shrink' || effect === 'grow') M.bounceImpulse(bounce, now() - t0);
    else if (effect === 'fly') M.flyPunch(fly, Math.random() * 2 * Math.PI);
  };
  const onPointerUp = () => { held = false; };
  // Capture, so nothing the editor bound ever sees a key while the stage is up.
  const swallow = (e) => {
    if (e.type === 'keydown' && e.key === 'Escape') { closeLogoStage(); return; }
    e.preventDefault?.();
    e.stopImmediatePropagation?.();
  };

  host.addEventListener('pointermove', onPointerMove);
  host.addEventListener('pointerdown', onPointerDown);
  for (const type of ['pointerup', 'pointercancel', 'pointerleave']) host.addEventListener(type, onPointerUp);
  for (const type of SWALLOWED) doc.addEventListener(type, swallow, true);
  globalThis.addEventListener?.('resize', fit);

  live = {
    name, effect, host, canvas,
    get size() { return size; },
    get position() { return { ...pos }; },
    get cloudLive() { return cloud.live; },
    release() {
      leftAt = now() - t0;
      host.removeEventListener('pointermove', onPointerMove);
      host.removeEventListener('pointerdown', onPointerDown);
      for (const type of ['pointerup', 'pointercancel', 'pointerleave']) host.removeEventListener(type, onPointerUp);
      for (const type of SWALLOWED) doc.removeEventListener(type, swallow, true);
      globalThis.removeEventListener?.('resize', fit);
      if (reduced) { if (frame) cancel(frame); drop(); }
    },
  };
  t0 = now();
  step();
  return true;
};

// Tests and the facade: the stage now on screen, or null.
export const currentLogoStage = () => live;
