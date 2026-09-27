// The select dropdowns open through the dropdownMenu portal (js/ui/control/customSelect.js and
// js/ui/accent/picker.js): the open list sits on <body>, so each control's press-outside test
// must look inside the portaled menu as well as its own box. Split from dropdownMenu.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { createStubElement, installDom } from '../../helpers/dom.js';

// A node moves on insert, as in a real DOM, and querySelector hands back one child per selector.
const moving = (node) => {
  const detach = (c) => { const kids = c.parentNode?.children; if (kids?.includes(c)) kids.splice(kids.indexOf(c), 1); };
  const [append, before] = [node.appendChild, node.insertBefore];
  node.appendChild = (c) => { detach(c); return append(c); };
  node.insertBefore = (c, at) => { detach(c); c.parentNode = c.parentElement = node; return before(c, at); };
  node.append = (...cs) => cs.forEach(node.appendChild);
  return node;
};
const element = (tag) => {
  const memo = new Map();
  const node = moving(createStubElement(tag, {
    querySelector: (sel) => memo.get(sel) ?? memo.set(sel, node.appendChild(element('span'))).get(sel),
  }));
  return node;
};
const portalDom = () => {
  const doc = installDom({ createElement: element }, {
    window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {} },
    HTMLSelectElement: class { get value() { return this._value; } set value(v) { this._value = v; } },
  });
  moving(doc.body);
  return doc;
};
// Open from the trigger, press inside the portaled menu, then press outside it.
const drive = (doc, trigger, menu) => {
  const states = [];
  const snap = () => states.push({ open: !menu.hidden, onBody: menu.parentElement === doc.body, portal: menu.classList.contains('dd-portal') });
  trigger.dispatch('click', { preventDefault() {} });
  snap();
  doc.dispatch('pointerdown', { target: menu.children.at(-1) });
  snap();
  doc.dispatch('pointerdown', { target: element('div') });
  snap();
  return states;
};
const EXPECTED = [
  { open: true, onBody: true, portal: true },
  { open: true, onBody: true, portal: true },
  { open: false, onBody: false, portal: false },
];

test('every select dropdown goes through the portal, and its press-outside sees it', async () => {
  const doc = portalDom();
  try {
    const { enhanceSelect } = await import('../../../js/ui/control/customSelect.js');
    const host = element('div');
    const select = host.appendChild(element('select'));
    Object.assign(select, { _value: 'a', options: [{ value: 'a', textContent: 'A' }, { value: 'b', textContent: 'B' }] });
    enhanceSelect(select);
    const wrap = select.parentNode;
    const [trigger, menu] = wrap.children.slice(-2);
    // The menu is on <body> while open, so an outside press must test it too.
    assert.deepEqual(drive(doc, trigger, menu), EXPECTED, 'customSelect portals, and a press inside its own menu keeps it');
    assert.equal(menu.parentElement, wrap, 'and closing puts it back');

    const { buildAccentPicker } = await import('../../../js/ui/accent/picker.js');
    const mount = element('div');
    buildAccentPicker(mount, { current: 'violet', onSelect() {} });
    const pickerMenu = mount.querySelector('.accent-dd-menu');
    pickerMenu.hidden = true;   // the markup's own `hidden`, which the stub does not parse
    assert.deepEqual(drive(doc, mount.querySelector('.accent-dd-trigger'), pickerMenu), EXPECTED,
      'accentPicker portals, and a press inside its own menu keeps it');
  } finally {
    doc.restore();
  }
  const css = COMPONENTS_CSS;
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*position: fixed/, 'the portaled menu is viewport-positioned');
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*right: auto/, 'and anchored from the left it was given');
});
