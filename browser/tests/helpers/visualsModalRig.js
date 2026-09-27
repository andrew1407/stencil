// A wired Default Visuals modal (js/ui/visuals/modal.js) over the DOM stub: its selects are
// enhanced for real (customSelect.js), and its checkboxes, selects and Reset All drive a
// recording app. `element` is the node factory an enhanced select needs (moves on insert).
import { createStubElement, installDom } from './dom.js';
import { installMemoryStorage } from './memoryStorage.js';

export const element = (tag) => {
  const memo = new Map();
  const node = createStubElement(tag, {
    querySelector: (sel) => memo.get(sel) ?? memo.set(sel, node.appendChild(element('span'))).get(sel),
  });
  const [append, before] = [node.appendChild, node.insertBefore];
  const detach = (c) => { const kids = c.parentNode?.children; if (kids?.includes(c)) kids.splice(kids.indexOf(c), 1); };
  node.appendChild = (c) => { detach(c); return append(c); };
  node.insertBefore = (c, at) => { detach(c); c.parentNode = c.parentElement = node; return before(c, at); };
  node.append = (...cs) => cs.forEach(node.appendChild);
  return node;
};

// A <select> in a host of its own, holding `values` (each its own label).
export const selectOf = (values, value = values[0]) => {
  const s = element('div').appendChild(element('select'));
  s.dispatchEvent = (e) => { s.dispatch(e.type, {}); return true; };
  return Object.assign(s, { _value: value, options: values.map((v) => ({ value: v, textContent: v })) });
};

// The enhanced face of a select: [trigger, menu], the two nodes customSelect adds after it.
export const faceOf = (select) => select.parentNode.children.slice(-2);

// requestAnimationFrame callbacks wait here until a case runs them.
export const frames = [];

export const installVisualsDom = () => {
  installMemoryStorage();
  const win = createStubElement('window', { innerWidth: 1200, innerHeight: 800 });
  win.dispatchEvent = (e) => { win.dispatch(e.type, { detail: e.detail }); return true; };
  return installDom({ autoCreateById: true, createElement: element }, {
    window: win,
    HTMLSelectElement: class { get value() { return this._value; } set value(v) { this._value = v; } },
    matchMedia: () => ({ matches: false }),
    requestAnimationFrame: (fn) => frames.push(fn),
  });
};

export const mountVisuals = async (doc, modes) => {
  const body = createStubElement('div');
  doc.register('visuals-modal-overlay', createStubElement('div', { querySelector: (s) => (s === '.settings-body' ? body : null) }));
  doc.register('vs-accent', element('div'));
  for (const [id, values] of [['vs-appearance', ['system', 'light', 'dark']], ['vs-motion-mode', modes],
    ['vs-style', ['solid', 'dashed', 'dotted']], ['vs-notify-channel', ['toast', 'system']]])
    doc.register(id, selectOf(values));
  const calls = [];
  const app = {
    accent: 'violet', customAccent: null, color: '#000000', thickness: 2, pointSize: 4, style: 'solid',
    accents: { themeMode: 'system', setThemeMode() {}, setAccent() {} },
    settings: { setMotion: (k, v) => calls.push([k, v]), setNotifyChannel() {}, setVisualColor() {} },
    renderer: { redraw() {} }, storage: { saveSoon() {} }, input: { setHoldDrawDelay() {} },
  };
  const { StencilVisualsModal } = await import('../../js/ui/visuals/modal.js');
  StencilVisualsModal.prototype.wire.call(createStubElement('stencil-visuals-modal'), app);
  return { app, calls, overlay: doc.getElementById('visuals-modal-overlay') };
};
