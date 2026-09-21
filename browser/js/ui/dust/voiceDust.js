// Voice dust + ray ring: while a mic listens, motes leave its tile and 24 spokes turn and
// breathe with the level — spawn rate scales as level SQUARED, so room hum stays quiet.
// One canvas per wearer, punched out under the tile's own rounded rect so both pass behind the
// icon; the rAF loop runs only while the tile listens or motes are in flight. Level is the
// `--voice-level` <html> variable (ui/toolbar.js, ~60Hz); the pure parts are node-testable.
import { dustEnabled } from '../motion.js';

export const DUST_MARGIN = 56;            // canvas room around the tile, px
const DUST_MAX_LIVE = 540;         // motes alive at once, whatever the shouting
export const DUST_SILENCE = 0.06;         // below this level nothing is born
export const DUST_LIFE_MS = Object.freeze([420, 900]);   // a mote's life, min..max
export const DUST_RATE = 48;              // motes per frame at full level
export const DUST_TINTS = 6;              // palette stops, ink → accent
export const RING_SPOKES = 24;
export const RING_SPIN_MS = 8000;         // one revolution (the logo's logoRaysSpin clock)
export const RING_BEAT_MS = 1200;         // the shimmer beat (logoPulse's)
const RING_WIDTH = 1.2;            // hairline, px
// How far the spokes reach from the tile's centre: past its corners, further with the voice.
export const ringRadius = (w, h, level) => Math.hypot(w / 2, h / 2) + 8 + 14 * level;
// The ring's opacity at time t: 0.35 ↔ 0.6 on the beat.
export const ringAlpha = (t) => 0.35 + 0.25 * (0.5 - 0.5 * Math.cos((2 * Math.PI * t) / RING_BEAT_MS));
// Spoke angles at time t: evenly spaced, the whole set turning with RING_SPIN_MS.
export const ringAngles = (t) => {
  const a0 = ((t % RING_SPIN_MS) / RING_SPIN_MS) * 2 * Math.PI;
  return Array.from({ length: RING_SPOKES }, (_, i) => a0 + (i / RING_SPOKES) * 2 * Math.PI);
};

// 'rgb(r, g, b)' / 'rgba(…)' / '#rrggbb' → [r, g, b]; anything else → null.
export const parseRgb = (str) => {
  const s = String(str || '').trim();
  let m = s.match(/^rgba?\(\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)/i);
  if (m) return [Number(m[1]), Number(m[2]), Number(m[3])];
  m = s.match(/^#([0-9a-f]{6})$/i);
  if (m) return [0, 2, 4].map((i) => parseInt(m[1].slice(i, i + 2), 16));
  return null;
};
export const mixRgb = (a, b, t) => a.map((v, i) => Math.round(v + (b[i] - v) * t));
// Evenly spaced tints from the ink to the accent: ink, two greys leaning accent, light
// accent, accent-ish, accent. Falls back to the accent alone when a colour won't parse.
export const dustPalette = (ink, accent, stops = DUST_TINTS) => {
  const a = parseRgb(accent), i = parseRgb(ink);
  if (!a) return [String(accent || '#7c3aed')];
  if (!i) return [`rgb(${a.join(', ')})`];
  return Array.from({ length: stops }, (_, k) => `rgb(${mixRgb(i, a, k / (stops - 1)).join(', ')})`);
};

// Motes to spawn this frame: DUST_RATE·level² per 60 Hz frame, scaled by the real dt,
// with the fractional part rolled so a low rate still yields the odd mote.
export const spawnCount = (level, dt = 16.7, rnd = Math.random) => {
  if (!(level > DUST_SILENCE)) return 0;
  const n = DUST_RATE * level * level * (dt / 16.7);
  return Math.floor(n) + (rnd() < n % 1 ? 1 : 0);
};

// Where a ray at `angle` from the centre of a w×h tile meets its edge.
export const edgePoint = (w, h, angle) => {
  const c = Math.cos(angle), sn = Math.sin(angle);
  const r = 1 / Math.max(Math.abs(c) / (w / 2), Math.abs(sn) / (h / 2));
  return { x: c * r, y: sn * r };
};

// A mote born on the edge of a w×h tile centred at (0,0): a uniformly random direction all the
// way round with a little angular jitter, so the corners fill as evenly as the sides.
export const newMote = (w, h, level, rnd = Math.random) => {
  const angle = rnd() * 2 * Math.PI;
  const { x, y } = edgePoint(w, h, angle);
  const speed = (22 + 90 * level) * (0.6 + 0.8 * rnd());   // px/s
  const dir = angle + (rnd() - 0.5) * 0.5;                  // ±0.25 rad of scatter
  return {
    x, y,
    vx: Math.cos(dir) * speed,
    vy: Math.sin(dir) * speed,
    age: 0,
    life: DUST_LIFE_MS[0] + rnd() * (DUST_LIFE_MS[1] - DUST_LIFE_MS[0]),
    size: 0.5 + rnd() * 1.2 + 0.5 * level,
    tint: Math.floor(rnd() * DUST_TINTS),   // which palette stop paints it
  };
};

// Advance a mote by dt ms: drift with drag, age; false once its life is spent.
export const stepMote = (m, dt) => {
  const k = dt / 1000;
  m.x += m.vx * k;
  m.y += m.vy * k;
  const drag = Math.exp(-dt / 500);
  m.vx *= drag;
  m.vy *= drag;
  m.age += dt;
  return m.age < m.life;
};

// Opacity over a life: quick in, long ease-out.
export const moteAlpha = (m) => {
  const p = m.age / m.life;
  return p < 0.1 ? p / 0.1 : 1 - (p - 0.1) / 0.9;
};

// Canvas fills take no var(), so a hidden probe span is painted with each variable and read
// back. Re-read every second, so a theme or accent swap recolours the next motes.
let probe = null;
const themePalette = () => {
  if (!probe) {
    probe = document.createElement('span');
    probe.style.cssText = 'position:fixed;left:-9999px;top:-9999px;width:0;height:0;pointer-events:none';
    document.body.appendChild(probe);
  }
  probe.style.color = 'var(--accent)';
  const accent = getComputedStyle(probe).color;
  probe.style.color = 'var(--text-main)';
  const ink = getComputedStyle(probe).color;
  return dustPalette(ink, accent);
};

// The canvas must paint ABOVE the wearer's own layer: a z-index one above the highest on the
// wearer's ancestor chain, never below 5.
export const layerAbove = (el, get = (typeof getComputedStyle === 'function' ? getComputedStyle : null)) => {
  let z = 4;
  for (let node = el; node && node.nodeType === 1 && get; node = node.parentElement) {
    const v = parseInt(get(node).zIndex, 10);
    if (Number.isFinite(v) && v > z) z = v;
  }
  return z + 1;
};

const currentLevel = () => {
  const v = parseFloat(document.documentElement.style.getPropertyValue('--voice-level'));
  return Number.isFinite(v) ? Math.max(0, Math.min(1, v)) : 0;
};

// Attach the emitter to a mic tile. `isOn` says whether the tile is listening (its
// own class); the loop starts when it turns on and winds down after it turns off.
export const attachVoiceDust = (el, isOn) => {
  if (!el || typeof document === 'undefined' || typeof requestAnimationFrame === 'undefined') return null;
  let canvas = null, ctx = null, frame = null, last = 0;
  let palette = null, paletteAt = 0;
  const motes = [];
  const ensure = () => {
    if (canvas) return canvas;
    canvas = document.createElement('canvas');
    canvas.className = 'voice-dust';
    canvas.setAttribute('aria-hidden', 'true');
    Object.assign(canvas.style, { position: 'fixed', pointerEvents: 'none', zIndex: String(layerAbove(el)), left: '0', top: '0' });
    document.body.appendChild(canvas);
    ctx = canvas.getContext('2d');
    return canvas;
  };
  const stop = () => {
    if (frame !== null) { cancelAnimationFrame(frame); frame = null; }
    motes.length = 0;
    if (canvas) { canvas.remove(); canvas = null; ctx = null; }
    last = 0;
  };
  const tick = (now) => {
    frame = null;
    // Motes and ring alike are particles, so they follow the dust gate: in 'slide'
    // (and 'none') the tile keeps only its own CSS pulse.
    const on = isOn() && dustEnabled();
    if (!on && !motes.length) { stop(); return; }
    const dt = last ? Math.min(50, now - last) : 16.7;
    last = now;
    const r = el.getBoundingClientRect();
    const level = on ? currentLevel() : 0;
    if (on) {
      const n = Math.min(spawnCount(level, dt), DUST_MAX_LIVE - motes.length);
      for (let i = 0; i < n; i++) motes.push(newMote(r.width, r.height, level));
    }
    for (let i = motes.length - 1; i >= 0; i--) if (!stepMote(motes[i], dt)) motes.splice(i, 1);
    if (!palette || now - paletteAt > 1000) { palette = themePalette(); paletteAt = now; }
    const c = ensure();
    const dpr = window.devicePixelRatio || 1;
    const w = r.width + 2 * DUST_MARGIN, h = r.height + 2 * DUST_MARGIN;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) {
      c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
      c.style.width = `${w}px`; c.style.height = `${h}px`;
    }
    c.style.transform = `translate(${r.left - DUST_MARGIN}px, ${r.top - DUST_MARGIN}px)`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const cx = w / 2, cy = h / 2;
    if (on) {
      // The ring: hairlines from the centre, fading at the tip; the tile punch-out below
      // clips them to start at its edge.
      const R = ringRadius(r.width, r.height, level);
      const light = palette[Math.min(3, palette.length - 1)];
      const g = ctx.createRadialGradient(cx, cy, R * 0.72, cx, cy, R);
      g.addColorStop(0, light);
      g.addColorStop(1, light.startsWith('rgb(') ? light.replace('rgb(', 'rgba(').replace(')', ', 0)') : 'transparent');
      ctx.strokeStyle = g;
      ctx.lineWidth = RING_WIDTH;
      ctx.lineCap = 'round';
      ctx.globalAlpha = ringAlpha(now);
      ctx.beginPath();
      for (const a of ringAngles(now)) {
        ctx.moveTo(cx, cy);
        ctx.lineTo(cx + Math.cos(a) * R, cy + Math.sin(a) * R);
      }
      ctx.stroke();
      ctx.globalAlpha = 1;
    }
    if (motes.length) {
      for (const m of motes) {
        ctx.fillStyle = palette[m.tint % palette.length];
        ctx.globalAlpha = 0.9 * moteAlpha(m);
        ctx.beginPath();
        ctx.arc(cx + m.x, cy + m.y, m.size / 2, 0, Math.PI * 2);
        ctx.fill();
      }
      ctx.globalAlpha = 1;
    }
    // The tile's rounded rectangle is cleared out of the frame so ring and motes pass under the
    // icon, while neighbouring controls stay under the cloud.
    const rad = parseFloat(getComputedStyle(el).borderTopLeftRadius) || 0;
    ctx.globalCompositeOperation = 'destination-out';
    ctx.beginPath();
    if (ctx.roundRect) ctx.roundRect(DUST_MARGIN, DUST_MARGIN, r.width, r.height, rad);
    else ctx.rect(DUST_MARGIN, DUST_MARGIN, r.width, r.height);
    ctx.fill();
    ctx.globalCompositeOperation = 'source-over';
    frame = requestAnimationFrame(tick);
  };
  const start = () => { if (frame === null) frame = requestAnimationFrame(tick); };
  // The wearer's class flips when it starts/stops listening — that is the trigger.
  const mo = new MutationObserver(() => { if (isOn()) start(); });
  mo.observe(el, { attributes: true, attributeFilter: ['class'] });
  if (isOn()) start();
  return { start, stop, get live() { return motes.length; } };
};
