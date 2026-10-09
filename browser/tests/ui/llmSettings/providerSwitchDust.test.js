// The assistant settings' provider switch (js/ui/llmSettings/modal.js) played through its real
// select on a stub page laid out like the window: rows come and go, the box eases between its
// heights (easeBoxHeight.js) and re-centres, carrying the select. Every cloud the pick raises rides
// the control, frame by frame — never left where the select stood before the box moved (user report).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../helpers/dom.js';
import { installDustDom, rect, cloudAim, near } from '../../helpers/dustRig.js';
import { element, faceOf } from '../../helpers/visualsModalRig.js';
import { createMemoryStorage, installMemoryStorage } from '../../helpers/memoryStorage.js';

let clock = 0;
// A canvas whose disc grains (the dust style) are kept per painted frame, in its own coordinates.
const canvasEl = () => {
  const c = createStubElement('canvas', { painted: [] });
  const ctx = new Proxy({}, {
    get: (t, k) => (k in t ? t[k] : (...a) => {
      if (k === 'clearRect') c.painted = [];
      else if (k === 'arc') c.painted.push({ x: a[0], y: a[1], r: a[2] });
    }),
    set: (t, k, v) => { t[k] = v; return true; },
  });
  c.getContext = () => ctx;
  return c;
};
// Observers deliver after a frame's callbacks, oldest first, for every target whose size changed.
const observers = [];
class StubResizeObserver {
  constructor(cb) { this.cb = cb; this.seen = new Map(); observers.push(this); }
  observe(el) { this.seen.set(el, '0x0'); }
  disconnect() { this.seen.clear(); }
}
const layout = () => {
  for (const o of [...observers]) {
    let changed = false;
    for (const [el, was] of o.seen) {
      const r = el.getBoundingClientRect();
      const now = `${r.width}x${r.height}`;
      if (now !== was) { o.seen.set(el, now); changed = true; }
    }
    if (changed) o.cb([]);
  }
};

installMemoryStorage();
globalThis.sessionStorage = createMemoryStorage();
globalThis.customElements = { define: () => {}, get: () => undefined };
globalThis.HTMLElement = class {};
globalThis.fetch = () => Promise.reject(new Error('offline'));
const dust = installDustDom({
  docOpts: { autoCreateById: true, createElement: (tag) => (tag === 'canvas' ? canvasEl() : element(tag)) },
  globals: {
    performance: { now: () => clock },
    ResizeObserver: StubResizeObserver,
    window: { innerWidth: 1280, innerHeight: 800, addEventListener() {}, removeEventListener() {}, dispatchEvent() {} },
    HTMLSelectElement: class { get value() { return this._value; } set value(v) { this._value = v; } },
  },
});
const { doc } = dust;

// The window's layout: a centred box whose height is its rows' unless the ease holds it.
const ROWS = [['provider', 45], ['chat-base-url-row', 45], ['chat-model-row', 45], ['chat-api-key-row', 45],
  ['chat-session-key-row', 45], ['chat-session-key-status-row', 45], ['chat-server-row', 45],
  ['chat-server-status-row', 30], ['chat-cors-note', 36], ['chat-session-key-note', 54]];
const CHROME_H = 180;       // header, section titles, history row, footer
const TRIGGER_DY = 94;      // the provider select's top below the box's
const rows = ROWS.map(([id, h]) => {
  const row = createStubElement('div', { getAnimations: () => [] });
  row.getBoundingClientRect = () => rect(379, 0, 522, row.style.display === 'none' ? 0 : h);
  return id === 'provider' ? row : doc.register(id, row);
});
const natural = () => CHROME_H + rows.reduce((h, row) => h + row.getBoundingClientRect().height, 0);
let ease = null;   // the box's height flight, played off the hand-cranked clock
const shownH = () => (ease && !ease.cancelled
  ? ease.from + (ease.to - ease.from) * Math.min(1, (clock - ease.start) / ease.ms)
  : parseFloat(box.style.height) || natural());
const boxTop = () => (800 - shownH()) / 2;
const box = createStubElement('div', {
  getBoundingClientRect: () => rect(360, boxTop(), 560, shownH()),
  animate: ([a, b], { duration }) => {
    ease = { from: parseFloat(a.height), to: parseFloat(b.height), ms: duration, start: clock, cancelled: false,
             finished: new Promise(() => {}), cancel() { this.cancelled = true; } };
    return ease;
  },
});
Object.defineProperty(box, 'offsetHeight', { get: shownH });
const body = createStubElement('div', { children: rows });
const overlay = doc.register('chat-settings-overlay', createStubElement('div', {
  querySelector: (sel) => ({ '.app-modal': box, '.settings-body': body })[sel]
    ?? (sel.startsWith('#') ? doc.getElementById(sel.slice(1)) : null),
}));

const OPTIONS = ['none', 'ollama', 'openai-compat', 'anthropic', 'stencil-server'];
const select = element('div').appendChild(element('select'));
select.dispatchEvent = (e) => { select.dispatch(e.type, {}); return true; };
Object.assign(select, { _value: 'none', options: OPTIONS.map((v) => ({ value: v, textContent: v })) });
doc.register('chat-provider', select);

const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');
const { MARK_IN_MS, SURFACE_MENU_OUT_MS } = await import('../../../js/ui/motion.js');
const { BOX_RESIZE_MS } = await import('../../../js/ui/motion/easeBoxHeight.js');
const { enhanceSelect } = await import('../../../js/ui/control/customSelect.js');
enhanceSelect(select);
const [trigger, menu] = faceOf(select);
const cur = trigger.querySelector('.cs-cur');
trigger.getBoundingClientRect = () => rect(577, boxTop() + TRIGGER_DY, 320, 30);
cur.getBoundingClientRect = () => rect(588, boxTop() + TRIGGER_DY + 7.5, 279, 15);
menu.getBoundingClientRect = () => rect(parseFloat(menu.style.left) || 0, parseFloat(menu.style.top) || 0, 320, 160);
Object.assign(menu, { offsetWidth: 320, offsetHeight: 160 });
const { StencilLlmSettingsModal } = await import('../../../js/ui/llmSettings/modal.js');
StencilLlmSettingsModal.prototype.wire.call(overlay, {});

test.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
const frame = () => { clock += 16; test.mock.timers.tick(16); dust.frame(); layout(); };
const frames = (ms) => { for (let t = 0; t < ms; t += 16) frame(); };
const inside = (p, r, pad = 0.5) => p.x >= r.left - pad && p.x <= r.right + pad && p.y >= r.top - pad && p.y <= r.bottom + pad;
const caret = () => { const r = trigger.getBoundingClientRect(); return { x: r.right - 14, y: r.top + 15 }; };

// The window open and settled on `from`, its list open and formed, then `to` picked from it.
const pick = (mode, from, to) => {
  setMotionPrefs({ mode });
  localStorage.setItem('drawingApp_llmSettings', JSON.stringify({ provider: from }));
  overlay.__stencilModal.close();
  frames(1200);
  overlay.__stencilModal.open();
  frames(1200);
  trigger.dispatch('click', { preventDefault() {} });
  frames(800);
  const clouds = new Set(dust.clouds());
  const opt = menu.children.find((li) => li.dataset?.value === to);
  const was = trigger.getBoundingClientRect();
  opt.dispatch('click');
  const raised = dust.clouds().filter((h) => !clouds.has(h));
  const at = new Map(raised.map((h) => [h, { left: parseFloat(h.style.left), top: parseFloat(h.style.top) }]));
  return { was, raised, at };
};
const shift = (host, at) => ({ x: parseFloat(host.style.left) - at.get(host).left,
                                y: parseFloat(host.style.top) - at.get(host).top });

for (const mode of ['particles', 'water', 'fire']) {
  test(`${mode}: every cloud of a provider switch that grows the window rides the select as it moves`, () => {
    const { was, raised, at } = pick(mode, 'none', 'anthropic');
    const flights = raised.map((h) => h.__cloud.flight).sort();
    assert.deepEqual(flights, ['fall', 'surfaceGather', 'surfaceScatter'], 'the old word falls, the new one forms, the list pours home');
    frame();
    // The first painted frame still shows the box at its old height: the ease starts there.
    assert.equal(trigger.getBoundingClientRect().top, was.top, 'the select is painted where it stood');
    let moved = 0;
    for (let t = 16; t < MARK_IN_MS; t += 16) {
      const r = cur.getBoundingClientRect();
      moved = Math.max(moved, was.top - trigger.getBoundingClientRect().top);
      for (const host of raised) {
        if (!host.parentNode || t >= host.__cloud.span) continue;   // landed, or faded out
        const d = shift(host, at);
        if (host.__cloud.flight === 'surfaceScatter') {
          const aim = cloudAim(host);
          assert.ok(near({ x: aim.x + d.x, y: aim.y + d.y }, caret()), `${t}ms: the list pours into the caret where it is now`);
          continue;
        }
        // Each grain's home, where a fall starts and a gather lands: on the word as painted now.
        for (const m of host.__cloud.motes)
          assert.ok(inside({ x: m.x + d.x, y: m.y + d.y }, r), `${t}ms: a ${host.__cloud.flight} grain is home on the word`);
      }
      frame();
    }
    assert.ok(moved > 80, `the box re-centred under the select (${moved}px)`);
    assert.ok(ease && clock - ease.start >= BOX_RESIZE_MS, 'and the ease has landed by the time the word has');
  });
}

test('dust: the grains are PAINTED on the select as it moves, from the first frame to the last', () => {
  const { raised, at } = pick('particles', 'none', 'anthropic');
  const paintedOn = (host) => {
    const d = shift(host, at);
    const r = cur.getBoundingClientRect();
    const c = host.children.find((n) => n.tagName === 'CANVAS');
    const grains = c.painted.map((g) => ({ x: g.x + d.x, y: g.y + d.y }));
    return grains.length && grains.every((g) => inside(g, r, 4));
  };
  const [fall, gather] = ['fall', 'surfaceGather'].map((f) => raised.find((h) => h.__cloud.flight === f));
  frame();
  assert.ok(paintedOn(fall), 'the old word breaks up where it is painted');
  frames(MARK_IN_MS - 32);
  assert.ok(paintedOn(gather), 'and the new one lands on the select, not on its old place');
});

test('a switch that shrinks the window: the list still pours into the caret as painted', () => {
  const { was, raised, at } = pick('particles', 'anthropic', 'none');
  const list = raised.find((h) => h.__cloud.flight === 'surfaceScatter');
  frame();
  assert.equal(trigger.getBoundingClientRect().top, was.top);
  for (let t = 16; t < SURFACE_MENU_OUT_MS; t += 16) {
    const d = shift(list, at);
    const aim = cloudAim(list);
    assert.ok(near({ x: aim.x + d.x, y: aim.y + d.y }, caret()), `${t}ms`);
    frame();
  }
  assert.ok(trigger.getBoundingClientRect().top > was.top + 40, 'the select moved down under it');
});

test('slide and none raise no cloud to strand; slide still eases the box', () => {
  for (const [mode, eases] of [['slide', true], ['none', false]]) {
    ease = null;
    const { raised } = pick(mode, 'none', 'anthropic');
    frame();
    assert.deepEqual(raised, [], `${mode}: nothing flies`);
    assert.equal(!!ease, eases, `${mode}: the box ${eases ? 'eases' : 'snaps'}`);
  }
});
