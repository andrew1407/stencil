// The voice dust as painted: each mic face starts it off its own listening class, the motion
// gate and --voice-level steer it, and one frame on a recording 2D context shows the ring, the
// motes, the palette, the punch-out and the layer. The maths is pinned in voiceDust.test.js.
import { test } from 'node:test';
import assert from 'node:assert';
import { dustPalette, ringRadius, RING_SPOKES, RING_SPIN_MS, DUST_MARGIN, attachVoiceDust } from '../../../js/ui/dust/voiceDust.js';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { recordingCtx, argsOf, indexOf } from '../../helpers/recordingCtx.js';
import { setMotionOverride } from '../../../js/ui/motion/motionPrefs.js';
import { wireVoiceChatToggle } from '../../../js/ui/visuals/voiceToggle.js';
import { StencilChatPanel } from '../../../js/ui/chat/panel.js';
import { wireCtxAssistantChat } from '../../../js/ui/ctx/assistantChat.js';

// A page whose canvases record their 2D calls; animation frames and class observers run by hand.
const INK = 'rgb(255, 255, 255)', ACCENT = 'rgb(124, 58, 237)';
const rich = (tag, extra = {}) => createStubElement(tag, {
  prepend() {}, before() {}, after() {}, scrollTo() {}, replaceChildren() {}, nodeType: 1, ...extra,
});
const dustPage = ({ level = '0.8', reduced = false } = {}) => {
  const frames = [], observers = [], canvases = [], byId = new Map();
  const createElement = (tag) => {
    const el = rich(tag);
    if (tag !== 'canvas') return el;
    const { ctx, calls } = recordingCtx(el);
    Object.defineProperty(ctx, 'createRadialGradient', { value: (...a) => ({ stops: [], args: a, addColorStop(o, c) { this.stops.push([o, c]); } }) });
    Object.assign(el, { getContext: () => ctx, calls });
    canvases.push(el);
    return el;
  };
  const doc = installDom({ createElement, getElementById: (id) => byId.get(id) ?? byId.set(id, rich('div', { id })).get(id) }, {
    window: { devicePixelRatio: 1, addEventListener() {}, removeEventListener() {}, dispatchEvent: () => true },
    requestAnimationFrame: (fn) => frames.push(fn), cancelAnimationFrame() {},
    MutationObserver: class { constructor(fn) { this.fn = fn; observers.push(this); } observe(el) { this.el = el; } disconnect() {} },
    ResizeObserver: class { observe() {} disconnect() {} },
    matchMedia: () => ({ matches: reduced, addEventListener() {} }),
    getComputedStyle: (el) => ({ zIndex: el.z ?? 'auto', borderTopLeftRadius: '6px',
      color: ({ 'var(--accent)': ACCENT, 'var(--text-main)': INK })[el.style?.color] ?? 'rgb(0, 0, 0)' }),
  });
  doc.documentElement.style.setProperty('--voice-level', level);
  const tile = (el) => Object.assign(el, { getBoundingClientRect: () => ({ left: 100, top: 50, width: 40, height: 32 }) });
  // The wearer's class flipped: its observer asks isOn(), then the queued frames run once.
  const flip = (el, now = 1000) => {
    observers.filter((o) => o.el === el).forEach((o) => o.fn([]));
    frames.splice(0).forEach((f) => f(now));
  };
  return { doc, canvases, tile, flip, byId };
};
const attachOn = (el) => attachVoiceDust(el, () => true);
// One painted frame of a listening tile at `now`; Math.random pinned so every mote shares a tint.
const paintedFrame = ({ now = 1000, parentZ = null } = {}) => {
  const p = dustPage();
  const random = Math.random;
  Math.random = () => 0.5;
  try {
    const mic = p.tile(rich('button', { parentElement: parentZ ? rich('div', { z: parentZ }) : null }));
    attachOn(mic);
    p.flip(mic, now);
    return p.canvases[0];
  } finally { Math.random = random; p.doc.restore(); }
};

// The three mic faces wear it, keyed on their own listening class; the level is the
// toolbar's live --voice-level, so no second voice subscription exists.
test('every mic face attaches the dust off its listening class; the level is --voice-level', () => {
  const faces = [
    ['toolbar', 'active', (p) => {
      const mic = p.tile(rich('button'));
      wireVoiceChatToggle(mic, { voice: { supported: true, voiceChat: false, onLevel() {} } });
      return mic;
    }],
    ['panel', 'chat-voice-listening', (p) => {
      p.tile(p.doc.getElementById('chat-send'));
      StencilChatPanel.prototype.wire.call(rich('stencil-chat-panel'), {});
      return p.byId.get('chat-send');
    }],
    ['flyout', 'chat-voice-listening', (p) => {
      p.tile(p.doc.getElementById('ctx-assist-send'));
      wireCtxAssistantChat({}, { sending: () => false, menuIsOpen: () => false, setOnMenuClose() {} }, () => {});
      return p.byId.get('ctx-assist-send');
    }],
  ];
  for (const [name, listening, wire] of faces) {
    const p = dustPage();
    try {
      const btn = wire(p);
      btn.classList.add(listening === 'active' ? 'chat-voice-listening' : 'active');
      p.flip(btn);
      assert.equal(p.canvases.length, 0, `${name}: another class starts nothing`);
      btn.classList.add(listening);
      p.flip(btn);
      assert.equal(p.canvases.length, 1, `${name}: its own listening class starts the dust`);
      assert.ok(p.canvases[0].calls.length > 0, `${name}: and it paints`);
    } finally { p.doc.restore(); }
  }
  const radius = (level) => {
    const p = dustPage({ level });
    try {
      const mic = p.tile(rich('button'));
      wireVoiceChatToggle(mic, { voice: { supported: true, onLevel() {} } });
      mic.classList.add('active');
      p.flip(mic);
      const [cx, cy] = argsOf(p.canvases[0].calls, 'moveTo')[0];
      const [x, y] = argsOf(p.canvases[0].calls, 'lineTo')[0];
      return Math.hypot(x - cx, y - cy);
    } finally { p.doc.restore(); }
  };
  assert.ok(Math.abs(radius('0') - ringRadius(40, 32, 0)) < 1e-9 && Math.abs(radius('1') - ringRadius(40, 32, 1)) < 1e-9,
    'the ring reads --voice-level');
  for (const [why, gate] of [['reduced motion', { reduced: true }], ['a mode without particles', {}]]) {
    const p = dustPage(gate);
    if (!gate.reduced) setMotionOverride({ mode: 'slide' });
    try {
      const mic = p.tile(rich('button'));
      attachOn(mic);
      p.flip(mic);
      assert.equal(p.canvases.length, 0, `no dust under ${why}`);
    } finally { setMotionOverride(null); p.doc.restore(); }
  }
  // Behind the icon: the tile's rounded rectangle, and ONLY the tile, is punched out of the frame.
  const { calls } = paintedFrame();
  assert.ok(calls.some(([k, v]) => k === 'set:globalCompositeOperation' && v === 'destination-out'));
  assert.deepEqual(argsOf(calls, 'roundRect'), [[DUST_MARGIN, DUST_MARGIN, 40, 32, 6]], 'no neighbour is punched out');
  assert.equal(indexOf(calls, 'rect'), -1);
});

// Every mote wears its own stop of the theme's range; the style is set only when the stop changes.
test('each mote is painted in its own stop, from the theme\'s range', () => {
  const { calls } = paintedFrame();
  const fills = calls.filter(([k]) => k === 'set:fillStyle').map(([, v]) => v);
  assert.ok(argsOf(calls, 'arc').length > 1, 'a swarm of motes');
  assert.equal(fills.length, 1, 'one shared stop, so the style is set once, not per mote');
  assert.ok(dustPalette(INK, ACCENT).includes(fills[0]), 'the range comes from the theme');
});

// The ring is drawn under the tile punch-out, from the centre, on the spin clock.
test('the ring is painted from the centre, behind the icon, in the theme\'s light stop', () => {
  const { calls } = paintedFrame({ now: RING_SPIN_MS / 4 });
  const punch = calls.findIndex(([k, v]) => k === 'set:globalCompositeOperation' && v === 'destination-out');
  assert.ok(indexOf(calls, 'stroke') > 0 && punch > indexOf(calls, 'stroke'),
    'the ring is drawn before the tile is punched out — so it sits behind the icon');
  const [cx, cy] = [(40 + 2 * DUST_MARGIN) / 2, (32 + 2 * DUST_MARGIN) / 2];
  assert.deepEqual(argsOf(calls, 'moveTo'), Array(RING_SPOKES).fill([cx, cy]), 'spokes start at the centre; the punch-out cuts them to the edge');
  const tips = argsOf(calls, 'lineTo').map(([x, y]) => Math.atan2(y - cy, x - cx));
  assert.ok(Math.abs(tips[0] - Math.PI / 2) < 1e-9, 'aimed on the spin clock');
  const [stop] = calls.find(([k]) => k === 'set:strokeStyle')[1].stops;
  assert.deepEqual(stop, [0, dustPalette(INK, ACCENT)[3]], 'the light stop of the theme range');
});

test('the canvas takes the layer above its wearer\'s tallest ancestor', () => {
  assert.equal(paintedFrame({ parentZ: '120' }).style.zIndex, '121', 'the canvas takes that layer');
});
