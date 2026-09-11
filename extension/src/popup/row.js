// ── One list row: its thumbnail, badges, outline, buttons and lazy measurement. ──
import { fetchAsDataUrl } from '../lib/stencil.js';
import { projectNameColor } from '../lib/pins.js';
import { icon } from '../lib/icons.js';
import { setTip } from '../lib/tip.js';
import { editableSrc, pinnable } from '../lib/imageModel.js';
import { pinTargetMode } from '../lib/connections.js';
import { passesFilters } from '../lib/filters.js';
import { rowTitle, thumbInitialSrc, dimText, rowBadges, rowOutlineClass } from '../lib/rowModel.js';
import { filterLeave, createFilterTransition } from '../lib/motion.js';
import { listEl, PLAY_THUMB } from './panelDom.js';
import { state, rowKey, rowResource, isPinned, isOpened, isProjectRow } from './model.js';
import { previewCache, bindPreview } from './preview.js';
import { resolveSharedThumb } from './sharedPins.js';
import { filters, renderCount } from './filters.js';
import { openMenu, openMenuAt } from './rowMenu.js';
import { togglePin } from './pinActions.js';
import { pinWithPrompt } from './pinDialog.js';
import { bindRowGestures, bindRowDrag } from './gestures.js';
import { bindHoverHighlight } from './hoverHighlight.js';

// Measure unknown-size images only once their row scrolls into view (thumbnails
// use loading="lazy" too). All matching rows render up front so filtering shows all.
const measureObs = new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    measureObs.unobserve(e.target);
    measure(e.target._image, e.target._dimEl, e.target);
  }
}, { root: listEl, rootMargin: '200px' });

// A filter change animates BOTH ways (lib/motion.js): rows the filters no longer admit
// fade out where they stood while the arriving ones ramp in. Rows are keyed by source,
// so a re-render of the same set animates nothing. A ghost is on its way out and belongs
// to no image, so it must not be measured any more.
export const filterTransition = createFilterTransition({
  list: listEl,
  onLeave: (li) => measureObs.unobserve(li),
});

// ── Rows ──
export const renderRow = (image) => {
  const li = document.createElement('li');
  li.dataset.key = rowKey(image);   // the filter transition diffs renders by this
  const row = document.createElement('div');
  row.className = 'row';

  const thumb = document.createElement('img');
  thumb.className = 'thumb';
  // What the row shows (tooltip / thumb source / badges / outline) is lib/rowModel.js.
  const initSrc = thumbInitialSrc(image, PLAY_THUMB);
  if (initSrc) thumb.src = initSrc;
  thumb.loading = 'lazy';
  // No native title here: the floating hover preview would cover it (the info stays on the
  // name). A broken thumb is likely hotlink-protected — retry once via fetchAsDataUrl
  // (host permissions), reusing the preview cache; a frameless video gets the play glyph.
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
  // Shared (server) rows have no plain src — their download is Bearer-authed, so resolve
  // the thumbnail through the owning connection instead of letting a bare <img> 404.
  if (image.shared) resolveSharedThumb(image, thumb);

  const meta = document.createElement('div');
  meta.className = 'meta';
  const name = document.createElement('div');
  name.className = 'name clickable';
  name.textContent = image.name;
  setTip(name, rowTitle(image));
  // Project rows paint the name in the project's custom `color`, or a fixed neutral grey when
  // unset. Values are inlined (not a CSS var) so a stale-cached theme.css can't blank the name.
  if (isProjectRow(image)) {
    name.style.color = projectNameColor(image.color, '#80868f');
    name.style.textShadow = '0 1px 2px rgba(0,0,0,0.55)';
  }

  // Click → open in editor; double-click → quick crop (videos act on their frame).
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
  // Outline colour: GOLD = on a server, GRAY = local pin, plus the opened yellow.
  const outline = rowOutlineClass(image, {
    showServerPins: state.showServerPins, sharedSources: state.sharedSources, pinned: isPinned(image),
  });
  if (outline) row.classList.add(outline);
  // Already opened in an editor: clicking the row opens the resume/copy chooser
  // (see bindRowGestures / buildMenu).
  if (opened) row.classList.add('opened');
  meta.append(name, sub);

  // Pin toggle (shown only when the item has an openable source). Reflects the raw
  // pinned state so you can unpin even with "show pinned" off (which hides the outline).
  let pinBtn = null;
  if (pinnable(image)) {
    pinBtn = document.createElement('button');
    pinBtn.className = 'pin-btn' + (image.pinned ? ' active' : '');
    pinBtn.innerHTML = icon('pin', { size: 15 });
    setTip(pinBtn, image.pinned ? 'Unpin' : 'Pin to top', { label: true });
    pinBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      // Unpin directly; when pinning, offer the local / on-server picker (servers
      // connected) so you can store it remotely without opening the ⋯ menu.
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
  // Right-click anywhere on the row opens the same actions menu, at the cursor.
  row.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    openMenuAt(image, e.clientX, e.clientY);
  });

  row.append(thumb, meta, ...(pinBtn ? [pinBtn] : []), more);
  bindHoverHighlight(row, image);
  bindRowDrag(row, image);
  li.appendChild(row);
  listEl.appendChild(li);

  // Defer measuring unknown-size images (most CSS backgrounds) until the row
  // scrolls into view; the observer then measures and re-checks the size filter.
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
    // The now-known size may no longer match — drop the row, keep the counter synced.
    if (!passesFilters(image, filters)) {
      // The measurement disqualified it — a FILTER dropping the row, not a delete, so it
      // gets the light fade rather than the destructive scatter.
      filterLeave(li, () => {
        li.remove();
        state.filtered = state.filtered.filter(it => it !== image);
        renderCount();
      });
    }
  };
  probe.src = image.src;
};
