// The controls confirmModal.js injects under its message at runtime — askAlt's third button,
// choose's picker, prompt's field and its reason line — so the static markup (and the markup
// tests) stay unchanged. Desktop twin: support/modal/modalChromePrompt.cpp.
import { icon } from '../icons.js';

export const injectRow = (body, el) => {
  const wrap = document.createElement('div');
  wrap.className = 'confirm-choose-row';
  wrap.appendChild(el);
  body.appendChild(wrap);
};

export const altButton = (opts, onClick) => {
  const btn = document.createElement('button');
  btn.id = 'confirm-modal-alt';
  btn.className = 'btn-icon-text';
  // A glyph like the other two, or it reads as the odd one out.
  btn.innerHTML = icon(opts.altIcon || 'plus', { size: 14 }) + '<span></span>';
  btn.querySelector('span').textContent = opts.altLabel || 'Alternative';
  btn.addEventListener('click', onClick);
  return btn;
};

export const choiceSelect = (options) => {
  const sel = document.createElement('select');
  sel.className = 'confirm-choose-select';
  for (const o of (options || [])) {
    const opt = document.createElement('option');
    opt.value = o.value;
    opt.textContent = o.label != null ? o.label : o.value;
    sel.appendChild(opt);
  }
  return sel;
};

export const promptField = (opts) => {
  const multiline = !!opts.multiline;
  const inp = document.createElement(multiline ? 'textarea' : 'input');
  if (multiline) inp.rows = opts.rows || 3;
  else inp.type = 'text';
  inp.className = 'confirm-prompt-input';
  inp.value = opts.defaultValue || '';
  inp.addEventListener('keydown', e => e.stopPropagation());
  return inp;
};

export const promptReason = () => {
  const why = document.createElement('div');
  why.className = 'confirm-prompt-reason';
  why.style.display = 'none';
  return why;
};
