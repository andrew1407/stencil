// Where a hidden colour field's native picker opens (utils/dom.js anchorPickerInput, colorTrial.js
// openColorPicker): Chrome hangs the picker under the input's own box, so the input is laid exactly
// over the swatch or button that asked, at open time, even under a transformed ancestor that
// re-roots position:fixed; and that control's tooltip goes as the picker opens.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { TIP_SHOW_DELAY_MS } from '../../../../js/ui/motion.js';
import { installTooltipPage } from '../../../helpers/tooltipRig.js';

const page = await installTooltipPage();
const { anchorPickerInput } = await import('../../../../js/utils.js');
const { openColorPicker } = await import('../../../../js/ui/bindings/controls/colorTrial.js');
const { createSwatchPicker } = await import('../../../../js/ui/panel/lines/swatchPicker.js');

const px = (v) => Number.parseFloat(v);
// A hidden input whose painted box is its fixed left/top/size, mapped through an ancestor's
// translate (ox, oy) and scale s, as a transformed .container does to position:fixed.
const fieldUnder = ({ ox = 0, oy = 0, s = 1 } = {}) => {
  const input = { style: {}, picks: [] };
  input.getBoundingClientRect = () => ({
    left: ox + s * px(input.style.left), top: oy + s * px(input.style.top),
    width: s * px(input.style.width), height: s * px(input.style.height),
  });
  input.showPicker = () => input.picks.push(input.getBoundingClientRect());
  return input;
};
const button = (rect) => ({ getBoundingClientRect: () => ({ ...rect, right: rect.left + rect.width, bottom: rect.top + rect.height }) });
const near = (a, b) => assert.ok(Math.abs(a - b) < 1e-6, `${a} ≈ ${b}`);
const over = (box, rect) => { for (const k of ['left', 'top', 'width', 'height']) near(box[k], rect[k]); };

test('the hidden field is laid over the button, not under it or at the page corner', () => {
  const rect = { left: 411.6, top: 70.2, width: 26, height: 22 };
  const input = fieldUnder();
  anchorPickerInput(input, button(rect));
  assert.equal(input.style.position, 'fixed');
  over(input.getBoundingClientRect(), rect);
  const untouched = { style: {} };
  anchorPickerInput(untouched, null);
  assert.deepEqual(untouched.style, {}, 'no button, no rect: the field is left alone');
});

test('a transformed ancestor re-roots position:fixed, and the measured correction undoes it', () => {
  const rect = { left: 1026, top: 465, width: 26, height: 29 };
  for (const t of [{ ox: 744, oy: 70 }, { ox: -30, oy: 12, s: 2 }, { ox: 5, oy: 5, s: 0.5 }]) {
    const input = fieldUnder(t);
    anchorPickerInput(input, button(rect));
    over(input.getBoundingClientRect(), rect);
  }
});

test('openColorPicker places the field before the picker opens and drops the control tip', () => {
  const swatch = page.control('Line color', { rect: { left: 1026, top: 465, width: 26, height: 29 } });
  page.over(swatch);
  page.run(TIP_SHOW_DELAY_MS);
  assert.ok(page.visible(), 'the swatch tip is up');
  const input = fieldUnder({ ox: 300, oy: 40 });
  openColorPicker(input, swatch);
  assert.equal(input.picks.length, 1);
  over(input.picks[0], swatch.getBoundingClientRect());
  assert.equal(page.visible(), false, 'its tip goes as the picker opens');
  const visibleField = fieldUnder();
  visibleField.style = { left: '7px', top: '9px', width: '30px', height: '20px' };
  openColorPicker(visibleField, null);
  assert.deepEqual(visibleField.style, { left: '7px', top: '9px', width: '30px', height: '20px' },
    'a visible colour field opens where it already is');
});

test('a Lines-tab swatch picker opens over the swatch it is handed at open time', () => {
  const host = { kids: [], appendChild(el) { this.kids.push(el); } };
  globalThis.document.createElement = () => {
    const el = fieldUnder({ ox: 744, oy: 70 });
    el.addEventListener = () => {};
    el.setAttribute = () => {};
    return el;
  };
  const picker = createSwatchPicker(host);
  const [input] = host.kids;
  const app = { lines: [{ color: '#ff0000', points: [] }], color: '#00ff00', compareReadOnly: () => false };
  for (const rect of [{ left: 1031.5, top: 472.4, width: 14, height: 14 }, { left: 1163.5, top: 542.4, width: 14, height: 14 }]) {
    picker.open(app, 0, 'color', button(rect));
    over(input.picks.at(-1), rect);
  }
});

// The projects row menu's colour item hands over the clicked row's rect, so the picker opens there.
test('the projects picker opens over the menu row that asked, not at the row\'s far "⋯"', async () => {
  const { createColorPicker } = await import('../../../../js/ui/projects/colorPicker.js');
  const parent = { appendChild: (el) => { parent.child = el; } };
  const open = createColorPicker({ app: {}, list: { parentElement: parent }, render() {} });
  const input = parent.child;
  input.getBoundingClientRect = () => ({ left: px(input.style.left), top: px(input.style.top),
    width: px(input.style.width), height: px(input.style.height) });
  let picked = null;
  input.showPicker = () => { picked = { left: px(input.style.left), top: px(input.style.top) }; };
  const rowRect = { left: 480, top: 360, width: 160, height: 28 };
  open({ id: 'p1', color: '#112233' }, { getBoundingClientRect: () => rowRect });
  assert.deepEqual(picked, { left: 480, top: 360 });
});

// Chrome hangs its picker off a re-rooted fixed field shifted by the root's offset twice (the docked
// chat's band), so such a field opens from <body>, at the button, and goes home once the pick ends.
test('a re-rooted field opens from <body> and goes back home on the next touch or the commit', () => {
  const nest = (name) => {
    const n = { name, kids: [] };
    const take = (el) => { el.parentNode?.kids.splice(el.parentNode.kids.indexOf(el), 1); el.parentNode = n; };
    n.appendChild = (el) => { take(el); n.kids.push(el); return el; };
    n.insertBefore = (el, at) => { take(el); n.kids.splice(at ? n.kids.indexOf(at) : n.kids.length, 0, el); return el; };
    return n;
  };
  const body = nest('body'), shell = nest('.container');
  const logo = { getBoundingClientRect: () => ({ left: 370, top: 20, width: 32, height: 32 }) };
  const input = { style: {}, picks: [], ownerDocument: { body }, listeners: {} };
  input.addEventListener = (t, fn) => { input.listeners[t] = fn; };
  input.removeEventListener = (t, fn) => { if (input.listeners[t] === fn) delete input.listeners[t]; };
  input.getBoundingClientRect = () => {
    const ox = input.parentNode === body ? 0 : 350;
    return { left: ox + px(input.style.left), top: px(input.style.top), width: px(input.style.width), height: px(input.style.height) };
  };
  input.showPicker = () => input.picks.push({ at: input.parentNode.name, ...input.getBoundingClientRect() });
  Object.defineProperty(input, 'nextSibling', { get: () => input.parentNode.kids[input.parentNode.kids.indexOf(input) + 1] ?? null });
  const sibling = { name: 'after' };
  shell.appendChild(input);
  shell.appendChild(sibling);
  for (const close of [() => page.fire('pointerdown', {}), () => input.listeners.change()]) {
    openColorPicker(input, logo);
    const pick = input.picks.at(-1);
    assert.equal(pick.at, 'body');
    over(pick, logo.getBoundingClientRect());
    assert.equal(px(input.style.left), 370, 'from <body>, fixed is the viewport: no correction left');
    close();
    assert.deepEqual(shell.kids, [input, sibling], 'back in its own slot');
    assert.deepEqual(body.kids, []);
    assert.equal(input.listeners.change, undefined, 'the homing listener goes with it');
  }
  const plain = fieldUnder();
  plain.parentNode = shell;
  assert.equal(anchorPickerInput(plain, logo), null, 'a field fixed to the viewport stays put');
});
