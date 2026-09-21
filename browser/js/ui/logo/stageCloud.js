// The cloud around the stage's mark: motes born on its edge, posed by the shared styleFrame and
// shaped by the shared grain kit (cloud.js), so fire, water and dust look the same here as
// everywhere else. Desktop twin: app/LogoStageCloud.cpp.
import { styleFrame, tintOf, stopOfTint, dustMix, PARTICLE_STYLES } from '../dust/flight.js';
import { grainShape, headingOf, paletteCss } from '../dust/grain.js';
import { fillGrains, resolveColour } from '../dust/cloud.js';
import { STAGE, markEdge } from './stageRules.js';

const CLOUD = STAGE.cloud;
const lerp = (range, t) => range[0] + (range[1] - range[0]) * t;

// A mote leaving the mark's edge at a random heading: it drifts outward, ages, and fades.
// `reach` carries the hold's intensity into how far it travels, so the cloud widens under a press.
export const newStageMote = (size, reach = 1, rnd = Math.random, dir = null) => {
  // With a heading the grains form a PLUME clear of the mark: born a gap behind it, thrown
  // further back, so the trail reads beside the icon instead of under it. `dir`'s LENGTH is how
  // strongly it forms, so the ring becomes a tail as the mark picks up speed instead of at a step.
  const pull = dir ? Math.min(1, Math.hypot(dir.x, dir.y)) : 0;
  const spread = Math.PI - (Math.PI - CLOUD.tailSpreadTurns * 2 * Math.PI) * pull;
  const angle = pull > 0 ? Math.atan2(-dir.y, -dir.x) + (rnd() - 0.5) * 2 * spread
                         : rnd() * 2 * Math.PI;
  const speed = lerp(CLOUD.speedShare, rnd()) * size * reach * (1 + (CLOUD.tailSpeedScale - 1) * pull);
  // At rest a grain is born along the mark's own rounded-square outline — a ring of radius size/2
  // buries its four corners under the art — and slides out to the plume's gap as the tail forms.
  const ring = markEdge(size, angle);
  const gap = size * CLOUD.tailGapShare;
  const at = { x: ring.x + (Math.cos(angle) * gap - ring.x) * pull,
               y: ring.y + (Math.sin(angle) * gap - ring.y) * pull };
  return {
    x: at.x, y: at.y,
    vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed,
    age: 0, life: lerp(CLOUD.lifeMs, rnd()),
    r: lerp(CLOUD.sizeShare, rnd()) * size * 0.5,
    w: rnd(), len: size * 0.5,
  };
};

export const stepStageMote = (m, dt) => {
  const k = dt / 1000;
  m.x += m.vx * k; m.y += m.vy * k;
  const drag = Math.exp(-dt / 700);
  m.vx *= drag; m.vy *= drag;
  m.age += dt;
  return m.age < m.life;
};

// Quick in, long ease-out — the voice cloud's shape, so one fade reads across the app.
export const stageMoteAlpha = (m) => {
  const p = m.age / m.life;
  return p < 0.1 ? p / 0.1 : 1 - (p - 0.1) / 0.9;
};

export const spawnStageCount = (intensity, dt, rnd = Math.random) => {
  const n = CLOUD.rate * intensity * (dt / 16.7);
  return Math.floor(n) + (rnd() < n % 1 ? 1 : 0);
};

// One emitter per stage: `style` is a dustCloud style name, null for a show that flies none.
export const createStageCloud = (style) => {
  const motes = [];
  const code = PARTICLE_STYLES[style] ?? null;
  let colours = null;
  const out = {};
  const bucket = new Map();
  const poly = [];
  return {
    get live() { return motes.length; },
    // `boost` is the stage's: 1 at rest, more under the pointer, most while it is held down.
    step(dt, size, boost = 1, { dir = null, rnd = Math.random } = {}) {
      if (code === null) return 0;
      const want = Math.min(spawnStageCount(boost, dt, rnd), CLOUD.maxLive - motes.length);
      for (let i = 0; i < want; i++) motes.push(newStageMote(size, boost, rnd, dir));
      for (let i = motes.length - 1; i >= 0; i--) if (!stepStageMote(motes[i], dt)) motes.splice(i, 1);
      return motes.length;
    },
    // Painted around (x, y) in the same batched fills every other cloud uses. `scale` is the
    // mark's own: the grains grow out of the header logo with it and shrink back into it.
    draw(ctx, doc, x, y, tMs, scale = 1) {
      if (code === null || !motes.length) return;
      if (!colours) colours = paletteCss().map((css) => resolveColour(doc, css));
      for (const b of bucket.values()) b.length = 0;
      for (const m of motes) {
        const p = Math.min(1, m.age / m.life);
        styleFrame(code, p, p, m.w, m.len, tMs, out);
        const alpha = stageMoteAlpha(m) * out.glow;
        if (alpha < 0.01) continue;
        const tint = tintOf(m.w);
        const mix = tint < 0 && code === PARTICLE_STYLES.dust ? dustMix(m.w, false) : out.mix;
        const key = Math.min(colours.length - 1, stopOfTint(mix, tint)) * 10 + Math.round(alpha * 9);
        let b = bucket.get(key);
        if (!b) bucket.set(key, (b = []));
        b.push(x + (m.x + out.sx) * scale, y + (m.y + out.sy) * scale, m.r * out.scale * scale,
          grainShape(code, m.w), headingOf(m.vx, m.vy, false));
      }
      for (const [key, b] of bucket) {
        if (!b.length) continue;
        ctx.globalAlpha = (key % 10) / 9;
        ctx.fillStyle = colours[Math.floor(key / 10)];
        fillGrains(ctx, b, b.length / 5, poly);
      }
      ctx.globalAlpha = 1;
    },
  };
};
