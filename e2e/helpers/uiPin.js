// UI regression pins — the no-screenshot answer to "did anything the user sees move?".
//
// capturePin() walks a subtree and records, per element, a stable path, its sorted class
// list, a fixed set of computed styles, and (for leaves) its text. expectPin() deep-equals
// that against e2e/pins/<name>.json and prints the first differing paths, so a module move
// or a stylesheet split that changes the rendered result names itself.
//
// UPDATE_PINS=1 rewrites the baseline instead of asserting.
import { expect } from '@playwright/test';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const PINS_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../pins');

// The properties a pin records. Layout box, colour, type and the flex/stack geometry —
// enough that any visible change lands in one of them, few enough to stay readable.
export const PIN_PROPS = [
  'display', 'position', 'color', 'background-color', 'border-color', 'border-width',
  'border-radius', 'font-size', 'font-weight', 'font-family', 'padding', 'margin', 'gap',
  'width', 'height', 'opacity', 'transform', 'box-shadow', 'text-align', 'flex-direction',
  'justify-content', 'align-items', 'overflow', 'z-index',
];

// Values that say "nothing set here" — CSS initial values and the like — are dropped from
// EVERY capture. It costs no sensitivity: a property that moves AWAY from one of these
// still appears in the diff (as "—" → the new value, and the reverse), it just keeps the
// pins from carrying a page of defaults on every node.
const DROPPED = ['none', 'normal', 'auto', '', 'static', 'visible', 'start', 'row',
  '0px', '1', '400', 'rgba(0, 0, 0, 0)'];

const MAX_NODES = 600;      // a runaway root fails loudly instead of writing a 5 MB pin
const MAX_DIFFS = 15;       // how many differing paths a failure prints

// Runs in the page. Skips display:none subtrees (a pin is about what is on screen) but
// still counts them for nth-of-type, so paths stay stable as siblings hide and show.
const walkSubtree = ({ root, props, dropped, maxNodes }) => {
  const host = document.querySelector(root);
  if (!host) return { error: `no element matches "${root}"` };
  const round = (n) => Math.round(n * 10) / 10;
  const norm = (v) => String(v).replace(/(-?\d*\.?\d+)px/g, (m, n) => `${round(parseFloat(n))}px`).trim();
  const nodes = [];
  let truncated = false;
  const walk = (el, at) => {
    if (nodes.length >= maxNodes) { truncated = true; return; }
    const cs = getComputedStyle(el);
    if (cs.display === 'none') return;
    const style = {};
    for (const p of props) {
      const v = norm(cs.getPropertyValue(p));
      if (!dropped.includes(v)) style[p] = v;
    }
    // An icon's own <svg> is pinned; its geometry children are not — they are shape data,
    // not app CSS, and they would triple every pin that contains an icon.
    const tag = el.tagName.toLowerCase();
    const kids = tag === 'svg' ? [] : [...el.children];
    const entry = { path: at, tag, classes: [...el.classList].sort().join(' '), style };
    if (!kids.length && tag !== 'svg') {
      const text = (el.textContent || '').replace(/\s+/g, ' ').trim();
      if (text) entry.text = text.slice(0, 120);
    }
    nodes.push(entry);
    const seen = {};
    for (const kid of kids) {
      const kt = kid.tagName.toLowerCase();
      seen[kt] = (seen[kt] || 0) + 1;
      walk(kid, `${at}/${kt}[${seen[kt]}]`);
    }
  };
  walk(host, ':root');
  return { nodes, truncated };
};

// One raw capture — no settling.
const readPin = async (page, { root, props }) => {
  const out = await page.evaluate(walkSubtree,
    { root, props, dropped: DROPPED, maxNodes: MAX_NODES });
  if (out.error) throw new Error(`UI pin: ${out.error}`);
  if (out.truncated) throw new Error(`UI pin: "${root}" has more than ${MAX_NODES} elements — pin a narrower root`);
  return out.nodes;
};

// Capture, then re-capture until two consecutive reads agree: the app moves a lot (dust
// clouds, modal flights), and motion is frozen per-state rather than per-property.
export async function capturePin(page, { name, root, props = PIN_PROPS, tries = 10, gapMs = 120 }) {
  let last = await readPin(page, { root, props });
  for (let i = 0; i < tries; i++) {
    await page.waitForTimeout(gapMs);
    const next = await readPin(page, { root, props });
    if (JSON.stringify(next) === JSON.stringify(last)) return { name, root, nodes: next };
    last = next;
  }
  throw new Error(`UI pin "${name}" never settled — "${root}" is still animating after ${tries * gapMs}ms`);
}

// Human-readable differences, baseline vs actual, most useful first.
export function diffPins(baseline, actual) {
  const byPath = (pin) => new Map(pin.nodes.map((n) => [n.path, n]));
  const want = byPath(baseline);
  const got = byPath(actual);
  const out = [];
  for (const [p, w] of want) {
    const g = got.get(p);
    if (!g) { out.push(`${p}: gone (was <${w.tag} class="${w.classes}">)`); continue; }
    if (w.tag !== g.tag) out.push(`${p}: tag ${w.tag} → ${g.tag}`);
    if (w.classes !== g.classes) out.push(`${p}: class "${w.classes}" → "${g.classes}"`);
    if ((w.text || '') !== (g.text || '')) out.push(`${p}: text "${w.text || ''}" → "${g.text || ''}"`);
    for (const k of new Set([...Object.keys(w.style), ...Object.keys(g.style)]))
      if (w.style[k] !== g.style[k]) out.push(`${p}: ${k} "${w.style[k] ?? '—'}" → "${g.style[k] ?? '—'}"`);
  }
  for (const p of got.keys()) if (!want.has(p)) out.push(`${p}: new element <${got.get(p).tag}>`);
  return out;
}

// Capture the state and assert it against its baseline (or write it under UPDATE_PINS=1).
export async function expectPin(page, { name, root, props = PIN_PROPS }) {
  const pin = await capturePin(page, { name, root, props });
  const file = path.join(PINS_DIR, `${name}.json`);
  if (process.env.UPDATE_PINS) {
    fs.mkdirSync(PINS_DIR, { recursive: true });
    fs.writeFileSync(file, `${JSON.stringify(pin, null, 2)}\n`);
    return pin;
  }
  if (!fs.existsSync(file)) throw new Error(`No baseline for "${name}" — record it with UPDATE_PINS=1`);
  const baseline = JSON.parse(fs.readFileSync(file, 'utf8'));
  const diffs = diffPins(baseline, pin);
  const shown = diffs.slice(0, MAX_DIFFS);
  if (diffs.length > MAX_DIFFS) shown.push(`… and ${diffs.length - MAX_DIFFS} more`);
  expect(shown, `UI pin "${name}" (${root}) drifted — rerun with UPDATE_PINS=1 once the change is intended`).toEqual([]);
  return pin;
}

// Freeze the app's motion through its OWN switches: the browser facade's motionMode /
// the extension's StencilMotion, plus the OS preference both of them already honour
// (emulated by the caller). Also pins the theme so a pin never rides the host's palette.
export async function freezeMotion(page) {
  await page.emulateMedia({ colorScheme: 'light', reducedMotion: 'reduce' });
  await page.evaluate(() => {
    if (window.stencil) { window.stencil.motionMode = 'none'; window.stencil.darkTheme = false; }
    else if (window.StencilMotion) { window.StencilMotion.set('none'); window.StencilTheme?.set('light'); }
  });
}
