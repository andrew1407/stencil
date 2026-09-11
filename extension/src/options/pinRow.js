// ── One pinned-image row ────────────────────────────────────────────────────
import { setPinned, setPinKeywords, pinKeywords } from '../lib/pins.js';
import { leaveThenRemove, scatterGridFor } from '../lib/motion.js';
import { icon } from '../lib/icons.js';
import { setTip } from '../lib/tip.js';
import { hostLabel, liftDust, recoverThumb, pinKey } from './pinsDom.js';
import { renderPins } from './pins.js';

export const renderPinRow = (pin, serverSources) => {
  const li = document.createElement('li');
  li.className = 'pin-row';
  li.dataset.key = pinKey(pin);   // the filter transition diffs renders by this
  // Golden outline + server badge when this pinned image is also stored on a server.
  const onServer = !!(serverSources && pin.source && serverSources.has(pin.source));
  if (onServer) li.classList.add('shared');

  const thumb = document.createElement('img');
  thumb.className = 'pin-thumb';
  thumb.loading = 'lazy';
  thumb.alt = '';
  thumb.src = pin.source;
  recoverThumb(thumb, pin.source, pin.kind, pin.resource || '');

  const info = document.createElement('div');
  info.className = 'pin-info';
  const name = document.createElement('div');
  name.className = 'pin-name';
  name.textContent = pin.name || pin.source;
  setTip(name, pin.source);
  const sub = document.createElement('div');
  sub.className = 'pin-sub';
  const kindEl = document.createElement('span');
  kindEl.className = 'pin-kind';
  kindEl.textContent = pin.kind || 'image';
  const siteEl = document.createElement('span');
  siteEl.className = 'site';
  siteEl.textContent = hostLabel(pin.site);
  setTip(siteEl, pin.site);
  sub.append(kindEl, siteEl);

  // Keyword chips + an inline editor (comma/space separated). Saved via setPinKeywords.
  const kwRow = document.createElement('div');
  kwRow.className = 'pin-keywords';
  const kws = pinKeywords(pin);
  for (const k of kws) {
    const chip = document.createElement('span');
    chip.className = 'pin-kw';
    chip.textContent = k;
    kwRow.appendChild(chip);
  }
  const editBtn = document.createElement('button');
  editBtn.type = 'button';
  editBtn.className = 'pin-kw-edit';
  editBtn.textContent = kws.length ? 'edit keywords' : '+ keywords';
  editBtn.addEventListener('click', () => {
    const input = document.createElement('input');
    input.type = 'text';
    input.className = 'pin-kw-input';
    input.value = kws.join(', ');
    input.placeholder = 'comma or space separated';
    let done = false;
    const save = async () => {
      if (done) return;
      done = true;
      await setPinKeywords(pin.site, pin.source, input.value.split(/[\s,]+/));
      renderPins();
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); save(); }
      else if (e.key === 'Escape') { done = true; renderPins(); }
    });
    input.addEventListener('blur', save);
    kwRow.replaceWith(input);
    input.focus();
    input.select();
  });
  kwRow.appendChild(editBtn);
  info.append(name, sub, kwRow);

  const actions = document.createElement('div');
  actions.className = 'pin-actions';
  const open = document.createElement('button');
  open.className = 'pin-btn';
  setTip(open, 'Open in new tab', { label: true });
  open.innerHTML = icon('external', { size: 15 });
  open.addEventListener('click', () => { if (pin.source) chrome.tabs.create({ url: pin.source }); });
  const unpin = document.createElement('button');
  unpin.className = 'pin-btn danger';
  setTip(unpin, 'Unpin', { label: true });
  unpin.innerHTML = icon('x', { size: 15 });
  unpin.addEventListener('click', async () => {
    // A DELETE, not a filter: the row scatters (the heavier effect) and leaves the DOM
    // before the rebuild, so the filter transition can't also fade it out.
    const played = leaveThenRemove(li, () => li.remove(), scatterGridFor(1));
    liftDust(li);
    await played;
    await setPinned({ source: pin.source, site: pin.site, pinned: false });
    renderPins();   // storage.onChanged also fires, but re-render now for instant feedback
  });
  actions.append(open, unpin);

  li.append(thumb, info, actions);
  return li;
};
