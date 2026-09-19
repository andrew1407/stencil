// The chrome an enhanced <select> wears: the wrap, the trigger face, the popup <ul> and the
// widest-option width floor. ui/customSelect.js drives them; the native element stays the truth.
import { icon } from './icons.js';
import { escapeHtml } from './base.js';
import { pinWidestFace } from './motion.js';

// The box floors at the WIDEST option's width so a row of selects doesn't reshuffle — a FLOOR,
// not a pin. Capped, so one long server URL can't overrun its row.
const MAX_FIT_PX = 240;

export function buildSelectFace(selectEl) {
  const wrap = document.createElement('span');
  wrap.className = 'accent-dd cs-dd';
  selectEl.parentNode.insertBefore(wrap, selectEl);
  wrap.appendChild(selectEl);
  selectEl.classList.add('cs-native');

  const trigger = document.createElement('button');
  trigger.type = 'button';
  trigger.className = 'accent-dd-trigger';
  trigger.setAttribute('aria-haspopup', 'listbox');
  trigger.setAttribute('aria-expanded', 'false');
  // The trigger IS the control now, so it inherits the native select's hover-tooltip
  // attributes, or an enhanced control would go silent.
  if (selectEl.dataset.title) trigger.dataset.title = selectEl.dataset.title;
  for (const k of ['title', 'disabledReason', 'hkTitle'])
    if (selectEl.dataset[k] != null) trigger.dataset[k] = selectEl.dataset[k];
  // …and its enabled state, or a disabled <select> stays invisible here.
  const syncDisabled = () => {
    trigger.disabled = selectEl.disabled;
    trigger.classList.toggle('cs-disabled', selectEl.disabled);
  };
  syncDisabled();
  trigger.innerHTML =
    '<span class="cs-cur-icon" aria-hidden="true"></span>' +
    '<span class="accent-dd-name cs-cur"></span>' +
    `<span class="accent-dd-caret" aria-hidden="true">${icon('chevron-down', { size: 13 })}</span>`;
  const menu = document.createElement('ul');
  menu.className = 'accent-dd-menu';
  menu.setAttribute('role', 'listbox');
  menu.hidden = true;
  wrap.append(trigger, menu);
  const cur = trigger.querySelector('.cs-cur');
  const curIcon = trigger.querySelector('.cs-cur-icon');

  let fittedCount = -1;
  const fitToWidestOption = () => {
    if (fittedCount === selectEl.options.length || !selectEl.options.length) return;
    const faces = [...selectEl.options].map((o) => escapeHtml(o.textContent));
    // Only a measurement that MEANT something counts as done: a select enhanced inside a
    // window that is still display:none measures zero, and must fit again once it is up.
    if (pinWidestFace(cur, faces, { force: true, prop: 'minWidth', max: MAX_FIT_PX }) > 0)
      fittedCount = selectEl.options.length;
  };

  return { wrap, trigger, menu, cur, curIcon, syncDisabled, fitToWidestOption };
}
