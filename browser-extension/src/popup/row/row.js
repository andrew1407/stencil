import { fetchAsDataUrl } from '../../lib/stencil.js';
import { projectNameColor } from '../../lib/prefs/pins.js';
import { icon } from '../../lib/icons.js';
import { setTip } from '../../lib/tip/tip.js';
import { editableSrc, pinnable } from '../../lib/image/imageModel.js';
import { pinTargetMode } from '../../lib/connection/connections.js';
import { passesFilters } from '../../lib/highlight/filters.js';
import { rowTitle, thumbInitialSrc, dimText, rowBadges, rowOutlineClass } from '../../lib/rowModel.js';
import { filterLeave, createFilterTransition } from '../../lib/motion.js';
import { listEl, PLAY_THUMB } from '../panelDom.js';
import { state, rowKey, rowResource, isPinned, isOpened, isProjectRow } from '../list/model.js';
import { previewCache, bindPreview } from './preview.js';
import { resolveSharedThumb } from '../pin/sharedPins.js';
import { filters, renderCount } from '../list/filters.js';
import { openMenu, openMenuAt } from './rowMenu.js';
import { togglePin } from '../pin/pinActions.js';
import { pinWithPrompt } from '../pin/pinDialog.js';
import { bindRowGestures, bindRowDrag } from '../gestures.js';
import { bindHoverHighlight } from './hoverHighlight.js';

// Unknown-size images are measured only once their row scrolls into view.
const measureObs = new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    measureObs.unobserve(e.target);
    measure(e.target._image, e.target._dimEl, e.target);
  }
}, { root: listEl, rootMargin: '200px' });

// A leaving ghost belongs to no image, so it must not be measured any more.
export const filterTransition = createFilterTransition({
  list: listEl,
  onLeave: (li) => measureObs.unobserve(li),
});

export const renderRow = (image) => {
  const li = document.createElement('li');
  li.dataset.key = rowKey(image);   // the filter transition diffs renders by this
  const row = document.createElement('div');
  row.className = 'row';

  const thumb = document.createElement('img');
  thumb.className = 'thumb';
  const initSrc = thumbInitialSrc(image, PLAY_THUMB);
  if (initSrc) thumb.src = initSrc;
  thumb.loading = 'lazy';
  // A broken thumb is likely hotlink-protected: retry once through host permissions.
  thumb.addEventListener('error', async () => {
    if (image.kind === 'video') { thumb.src = PLAY_THUMB; return; }
    const src = editableSrc(image);
    if (thumb.dataset.recovered || !src || src.startsWith('data:')) { thumb.style.visibility = 'hidden'; return; }
    thumb.dataset.recovered = '1';
    try {
      const dataUrl = previewCache.get(src) || await fetchAsDataUrl(src, { pageUrl: rowResource(image) });
      previewCache.set(src, dataUrl);
      thumb.src = dataUrl;
    } catch {
      thumb.style.visibility = 'hidden';
    }
  });
  bindPreview(thumb, image);
  // A shared row's download is Bearer-authed; a bare <img> would 404.
  if (image.shared) resolveSharedThumb(image, thumb);

  const meta = document.createElement('div');
  meta.className = 'meta';
  const name = document.createElement('div');
  name.className = 'name clickable';
  name.textContent = image.name;
  setTip(name, rowTitle(image));
  // Inlined, not a CSS var: a stale-cached theme sheet must not blank the name.
  if (isProjectRow(image)) {
    name.style.color = projectNameColor(image.color, '#80868f');
    name.style.textShadow = '0 1px 2px rgba(0,0,0,0.55)';
  }

  bindRowGestures(thumb, image);
  bindRowGestures(name, image);
  const sub = document.createElement('div');
  sub.className = 'sub';
  const badgeSpan = (b) => {
    const span = document.createElement('span');
    span.className = b.cls;
    if (b.html) span.innerHTML = b.html;   // a fixed icon from lib/icons.js, never page data
    else span.textContent = b.text;
    if (b.title) span.dataset.title = b.title;
    return span;
  };
  const opened = isOpened(image);
  const badges = rowBadges(image, { opened });
  const openedBadge = opened ? badges.pop() : null;   // the opened flag renders after the size
  for (const b of badges) sub.appendChild(badgeSpan(b));
  const dim = document.createElement('span');
  dim.className = 'dim';
  dim.textContent = dimText(image);
  sub.appendChild(dim);
  if (openedBadge) sub.appendChild(badgeSpan(openedBadge));
  const outline = rowOutlineClass(image, {
    showServerPins: state.showServerPins, sharedSources: state.sharedSources, pinned: isPinned(image),
  });
  if (outline) row.classList.add(outline);
  if (opened) row.classList.add('opened');
  meta.append(name, sub);

  // Reflects the raw pinned state, so an unpin works even with "show pinned" off.
  let pinBtn = null;
  if (pinnable(image)) {
    pinBtn = document.createElement('button');
    pinBtn.className = 'pin-btn' + (image.pinned ? ' active' : '');
    pinBtn.innerHTML = icon('pin', { size: 15 });
    setTip(pinBtn, image.pinned ? 'Unpin' : 'Pin to top', { label: true });
    pinBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (image.pinned || pinTargetMode(state.connections) === 'none') togglePin(image);
      else pinWithPrompt(image, pinBtn);
    });
  }

  const more = document.createElement('button');
  more.className = 'more-btn';
  more.textContent = '⋯';
  setTip(more, 'Actions', { label: true });
  more.addEventListener('click', (e) => {
    e.stopPropagation();
    openMenu(more, image);
  });
  row.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    openMenuAt(image, e.clientX, e.clientY);
  });

  row.append(thumb, meta, ...(pinBtn ? [pinBtn] : []), more);
  bindHoverHighlight(row, image);
  bindRowDrag(row, image);
  li.appendChild(row);
  listEl.appendChild(li);

  if (!image.measured) {
    li._image = image;
    li._dimEl = dim;
    measureObs.observe(li);
  }
};

const measure = (image, dimEl, li) => {
  const probe = new Image();
  probe.onload = () => {
    image.w = probe.naturalWidth;
    image.h = probe.naturalHeight;
    image.measured = true;
    dimEl.textContent = dimText(image);
    // A filter dropping the row, not a delete: the light fade, not the scatter.
    if (!passesFilters(image, filters)) {
      filterLeave(li, () => {
        li.remove();
        state.filtered = state.filtered.filter(it => it !== image);
        renderCount();
      });
    }
  };
  probe.src = image.src;
};
