// The motion mode gate (src/lib/shellPrefs.js StencilMotion, the browser motionPrefs.js twin):
// what src/lib/motion.js reads live, and the one door every cloud is built behind.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { animationsCss, motionSrc } from './helpers/sources.js';

const css = animationsCss();

// ── The motion mode (lib/shellPrefs.js StencilMotion; browser motionPrefs.js twin) ──
test('the gates read StencilMotion live, and fall back to the OS preference without it', async () => {
  const m = await import('../src/lib/motion.js');
  const prev = globalThis.StencilMotion;
  try {
    delete globalThis.StencilMotion;
    assert.equal(m.motionMode(), 'particles');
    assert.equal(m.dustEnabled(), true, 'no script: the OS alone speaks, and it is fine with motion');
    assert.equal(m.particleStyle(), 'dust');
    assert.equal(m.wipeDurationMs(), Math.max(m.LEAVE_MS, m.DISINTEGRATE_MS));
    let mode = 'water';
    globalThis.StencilMotion = {
      get: () => mode,
      reduced: () => mode === 'none',
      particles: () => ['particles', 'water', 'fire'].includes(mode),
      style: () => ({ particles: 'dust', water: 'water', fire: 'fire' })[mode] || null,
    };
    assert.equal(m.motionMode(), 'water');
    assert.equal(m.dustEnabled(), true);
    assert.equal(m.particleStyle(), 'water');
    mode = 'slide';
    assert.equal(m.motionReduced(), false, 'slide still moves');
    assert.equal(m.dustEnabled(), false, '…just never out of particles');
    assert.equal(m.wipeDurationMs(), m.LEAVE_MS, 'a wipe is just the collapse then');
    // Every builder answers the gate: no cloud, no veil, the caller's own entrance.
    const el = { classList: new Set(), getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 20 }) };
    el.classList.add = (c) => Set.prototype.add.call(el.classList, c);
    assert.equal(m.disintegrate(el), false, 'disintegrate declines under slide');
    mode = 'none';
    assert.equal(m.motionReduced(), true);
    assert.equal(m.wipeDurationMs(), 0);
  } finally {
    if (prev === undefined) delete globalThis.StencilMotion; else globalThis.StencilMotion = prev;
  }
});

test('the mode is wired: the one cloud door, the chat slide, and the CSS half', () => {
  const src = motionSrc();
  const body = src.slice(src.indexOf('export function disintegrate('), src.indexOf('export const reintegrate'));
  assert.ok(body.includes('if (!dustEnabled()) return false;'), 'the cloud is built behind the gate');
  assert.ok(body.includes('const style = styleCode();') && body.includes('const paints = paletteCss();'),
            'a styled cloud is painted from the accent palette');
  assert.match(src, /if \(!dustEnabled\(\)\) \{ flashLanding\(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS\); return Promise\.resolve\(\); \}/);
  // No helper still reads the media query by hand — motionReduced() is the one gate.
  assert.equal((src.match(/matchMedia\('\(prefers-reduced-motion: reduce\)'\)/g) || []).length, 1, 'only prefersReducedMotion itself');
  assert.match(css, /:root\[data-motion="none"\] \*,/);
  assert.match(css, /\.chat-slide-in \{ animation: chatRiseIn/);
  // Every extension page stamps the mode pre-paint through the accent script set.
  const prefs = readFileSync(new URL('../src/lib/prefs/prefs.js', import.meta.url), 'utf8');
  assert.match(prefs, /setAttribute\('data-motion'/);
});
