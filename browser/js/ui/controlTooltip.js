// Instant control tooltip: shows a `title`/`data-title` on hover with no delay (the native
// one has a ~1s delay and never shows on disabled controls). While ours is up the element's
// `title` is blanked so the native one can't double-show. (tooltip.js is the canvas readout.)

const SHOW_DELAY_MS = 90;   // tiny delay so flicking the cursor across the bar doesn't flash tips

let tip = null;             // the floating element (created lazily)
let curEl = null;           // element whose tooltip is currently shown/pending
let showTimer = null;
let lastEvent = null;       // last pointer event, for positioning

const ensureTip = () => {
  if (tip) return tip;
  tip = document.createElement('div');
  tip.id = 'app-tooltip';
  tip.setAttribute('role', 'tooltip');
  document.body.appendChild(tip);
  return tip;
};

// Prefer the live composed `title` (carries the "(combo)" hint + "— reason" line); fall
// back to data-title for elements whose title hasn't been composed yet.
const textFor = (el) => {
  const t = el.getAttribute('title');
  if (t != null && t.trim() !== '') return t;
  const d = el.dataset ? el.dataset.title : '';
  return d || '';
};

const place = (e) => {
  if (!tip || !e) return;
  const pad = 10;
  const r = tip.getBoundingClientRect();
  let x = e.clientX + 14;
  let y = e.clientY + 18;
  if (x + r.width + pad > window.innerWidth) x = window.innerWidth - r.width - pad;
  if (y + r.height + pad > window.innerHeight) y = e.clientY - r.height - 12;
  if (x < pad) x = pad;
  if (y < pad) y = pad;
  tip.style.left = `${x}px`;
  tip.style.top = `${y}px`;
};

const hide = () => {
  clearTimeout(showTimer);
  showTimer = null;
  if (curEl) {
    // Restore the native title we suppressed (only if still blanked, so a live re-compose wins).
    const saved = curEl.__nativeTitle;
    if (saved != null && curEl.getAttribute('title') === '') curEl.setAttribute('title', saved);
    if (curEl.__nativeTitle != null) delete curEl.__nativeTitle;
    curEl = null;
  }
  if (tip) tip.classList.remove('visible');
};

const reveal = (el) => {
  const txt = textFor(el);
  if (!txt) return;
  // Suppress the native (delayed) tooltip while ours is visible.
  const native = el.getAttribute('title');
  if (native) {
    el.__nativeTitle = native;
    el.setAttribute('title', '');
  }
  const t = ensureTip();
  t.textContent = txt;        // pre-line CSS renders the "\n— reason" line as a second line
  t.classList.add('visible');
  place(lastEvent);
};

export const initTooltips = () => {
  if (typeof document === 'undefined') return;
  document.addEventListener('pointerover', (e) => {
    lastEvent = e;
    // Still inside the active target (e.g. moved onto its child icon) -> keep showing.
    if (curEl && curEl.contains(e.target)) return;
    const el = e.target.closest ? e.target.closest('[title], [data-title]') : null;
    if (!el || el === curEl) return;
    hide();
    curEl = el;
    showTimer = setTimeout(() => reveal(el), SHOW_DELAY_MS);
  });
  document.addEventListener('pointermove', (e) => {
    lastEvent = e;
    if (tip && tip.classList.contains('visible')) place(e);
  });
  document.addEventListener('pointerout', (e) => {
    if (!curEl) return;
    // Hide only when the pointer truly leaves the active element (not onto a descendant).
    if (!curEl.contains(e.relatedTarget)) hide();
  });
  // Never let a tooltip get stuck: drop it on any scroll / click / key / blur.
  document.addEventListener('scroll', hide, true);
  document.addEventListener('pointerdown', hide, true);
  document.addEventListener('keydown', hide, true);
  window.addEventListener('blur', hide);
};
