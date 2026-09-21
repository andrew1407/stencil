// An icon-only control says its name in `data-title` and nowhere else, and a settings field
// is named by a plain <label> beside it that carries no `for` — a screen reader reads both
// as unnamed. Both names are already on the page; this points the a11y tree at them.
import { parseTip } from './tip/tipContent.js';

const FIELD = new Set(['INPUT', 'SELECT', 'TEXTAREA']);
const FIELD_SEL = 'input, select, textarea';
// A field never takes its name from its content; a button, link or label does.
const unnamed = (el) => (FIELD.has(el.tagName) ? true : !el.textContent.trim());
const named = (el) => el.hasAttribute('aria-label') || el.hasAttribute('aria-labelledby');
let seq = 0;

/** The accessible name a control's tooltip implies; '' when it has none. */
export const controlLabel = (el) => parseTip(el.dataset.tip || el.dataset.title || '').title;

/** Name one control from its tooltip — unless it says its name in text, or another element says it. */
export const labelControl = (el) => {
  if (!el || !el.dataset || el.hasAttribute('aria-labelledby') || !unnamed(el)) return;
  const name = controlLabel(el);
  if (name) el.setAttribute('aria-label', name);
};

// A row is written `<label>Name</label><span><input></span>`, so a field's name is the label
// just before it — or before the wrapper it sits in.
const rowLabel = (el) => {
  for (let node = el; node; node = node.parentElement) {
    const prev = node.previousElementSibling;
    if (!prev) continue;
    return prev.tagName === 'LABEL' && !prev.hasAttribute('for') && !prev.querySelector(FIELD_SEL)
      ? prev : null;
  }
  return null;
};

// The label stays unassociated on purpose — a `for` would make it a click target too, and a
// label that toggles the checkbox beside it is a behaviour, not a name.
export const nameField = (el) => {
  if (!el || named(el) || el.type === 'hidden' || el.getAttribute('aria-hidden') === 'true') return;
  if (el.closest('label')) return;
  const label = rowLabel(el);
  if (label) {
    if (!label.id) label.id = `stencil-label-${++seq}`;
    el.setAttribute('aria-labelledby', label.id);
    return;
  }
  if (el.placeholder) el.setAttribute('aria-label', el.placeholder);
};

export const labelControls = (root = document) => {
  for (const el of root.querySelectorAll('[data-title]')) labelControl(el);
  for (const el of root.querySelectorAll(FIELD_SEL)) nameField(el);
};

// Install once: names every control on the page, every control a modal or panel renders
// later, and re-names one whose tooltip changes (the panel toggle's Hide↔Show).
export const watchControlLabels = (root = document.body) => {
  labelControls(document);
  const obs = new MutationObserver((records) => {
    for (const rec of records) {
      if (rec.type === 'attributes') { labelControl(rec.target); continue; }
      for (const node of rec.addedNodes) {
        if (node.nodeType !== 1) continue;
        if (node.matches?.('[data-title]')) labelControl(node);
        if (FIELD.has(node.tagName)) nameField(node);
        labelControls(node);
      }
    }
  });
  obs.observe(root, { childList: true, subtree: true, attributes: true, attributeFilter: ['data-title', 'data-tip'] });
  return obs;
};
