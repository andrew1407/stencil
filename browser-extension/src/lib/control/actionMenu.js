// The floating ⋯ action menu: one shared element per surface, which popup.js and
// mode.js fill from the same builders.
import { menuTransformOrigin } from '../chat/msgMenu.js';
import { surfaceIn, surfaceOut, settleSurface, centerOf } from '../motion.js';
import { icon } from '../icons.js';

const MARGIN = 6;   // px to a viewport edge

export const menuPlacement = ({ x, y, size, viewport }) => {
  if (x + size.width > viewport.width) x = Math.max(MARGIN, viewport.width - size.width - MARGIN);
  if (y + size.height > viewport.height) y = Math.max(MARGIN, viewport.height - size.height - MARGIN);
  return { left: Math.max(MARGIN, x), top: Math.max(MARGIN, y) };
};

export const anchoredX = ({ rect, width }) => {
  const x = rect.left - width - MARGIN;
  return x < MARGIN ? rect.right + MARGIN : x;
};

// Computed in viewport space, returned wrap-relative (the flyout is absolute in its wrap).
export const flyoutPlacement = ({ head, wrap, size, viewport }) => {
  let vLeft = head.right - 2;
  if (vLeft + size.width > viewport.width - MARGIN) vLeft = head.left - size.width + 2;
  vLeft = Math.max(MARGIN, Math.min(vLeft, viewport.width - size.width - MARGIN));
  const vTop = Math.max(MARGIN, Math.min(head.top - 5, viewport.height - size.height - MARGIN));
  return { left: vLeft - wrap.left, top: vTop - wrap.top };
};

// `run` wraps a clicked item's action in the caller's error reporting.
export const createActionMenu = ({ menuEl, run = (fn) => fn(), doc = document, win = window }) => {
  let anchorBtn = null;

  // The label is a TEXT NODE, never innerHTML: callers feed page-derived names.
  const item = (glyph, label, fn) => {
    const b = doc.createElement('button');
    b.type = 'button';
    const ic = doc.createElement('span');
    ic.className = 'ic';
    ic.innerHTML = glyph;   // a fixed icon from lib/icons.js, never user data
    b.append(ic, doc.createTextNode(label));
    b.addEventListener('click', async () => {
      close();
      await run(fn);
    });
    return b;
  };

  const sep = () => {
    const d = doc.createElement('div');
    d.className = 'sep';
    return d;
  };

  const label = (text) => {
    const d = doc.createElement('div');
    d.className = 'label';
    d.textContent = text;
    return d;
  };

  // CSS :hover controls the flyout's visibility; JS only positions it — absolute in its
  // wrap, NOT fixed, because the entrance animation leaves a transform matrix behind.
  const submenu = (iconHtml, labelText, children) => {
    const wrap = doc.createElement('div');
    wrap.className = 'submenu';
    const head = doc.createElement('button');
    head.className = 'submenu-head';
    head.type = 'button';
    head.innerHTML = `<span class="ic">${iconHtml}</span><span class="submenu-label">${labelText}</span>`
      + `<span class="caret">${icon('chevron-right', { size: 12 })}</span>`;
    const fly = doc.createElement('div');
    fly.className = 'flyout';
    for (const c of children) fly.append(c);
    const place = () => {
      const hr = head.getBoundingClientRect();
      const wr = wrap.getBoundingClientRect();
      fly.style.margin = '0';
      const p = flyoutPlacement({
        head: hr, wrap: wr,
        size: { width: fly.offsetWidth, height: fly.offsetHeight },
        viewport: { width: win.innerWidth, height: win.innerHeight },
      });
      fly.style.left = `${p.left}px`;
      fly.style.top = `${p.top}px`;
    };
    wrap.addEventListener('mouseenter', () => {
      place();
      surfaceIn(fly, centerOf(head));
    });
    wrap.addEventListener('mouseleave', () => {
      // Already display:none by mouseleave — unhide it for the frame the motes are cloned in.
      fly.style.display = 'block';
      surfaceOut(fly, centerOf(head));
      fly.style.display = '';
    });
    wrap.append(head, fly);
    return wrap;
  };

  // The point the menu came out of, so the close pours it back into the same icon.
  let openOrigin = null;

  // `origin` is the point the pop grows from and the particles fly out of.
  const place = (x, y, origin = { x, y }) => {
    const size = { width: menuEl.offsetWidth, height: menuEl.offsetHeight };
    const { left, top } = menuPlacement({
      x, y, size, viewport: { width: win.innerWidth, height: win.innerHeight },
    });
    menuEl.style.left = `${left}px`;
    menuEl.style.top = `${top}px`;
    menuEl.style.transformOrigin = menuTransformOrigin({ x: origin.x, y: origin.y, left, top, size });
    openOrigin = { x: origin.x, y: origin.y };
    surfaceIn(menuEl, openOrigin);
  };

  // Capture-phase so it wins over the surface's own handlers; armed only while open.
  const onEscape = (e) => {
    if (e.key !== 'Escape') return;
    e.preventDefault();
    e.stopPropagation();
    close();
  };
  const armEscape = () => doc.addEventListener('keydown', onEscape, true);

  // The anchor is marked BEFORE `fill` builds the contents (fill may capture it).
  const openAnchored = (btn, fill) => {
    if (anchorBtn === btn) return close();
    close();
    anchorBtn = btn;
    btn.classList.add('active');
    fill();
    menuEl.hidden = false;
    const r = btn.getBoundingClientRect();
    place(anchoredX({ rect: r, width: menuEl.offsetWidth }), r.top,
      { x: r.left + r.width / 2, y: r.top + r.height / 2 });
    armEscape();
  };

  const openNodes = (btn, nodes) => openAnchored(btn, () => {
    menuEl.innerHTML = '';
    menuEl.append(...nodes);
  });

  const openAt = (x, y, fill) => {
    close();
    fill();
    menuEl.hidden = false;
    place(x, y);
    armEscape();
  };

  const close = () => {
    // Dusted out FIRST, while still measurable; the close stays synchronous so a burst
    // of open/close lands on the true state.
    if (!menuEl.hidden) surfaceOut(menuEl, openOrigin);
    else settleSurface(menuEl);
    menuEl.hidden = true;
    menuEl.innerHTML = '';
    if (anchorBtn) anchorBtn.classList.remove('active');
    anchorBtn = null;
    doc.removeEventListener('keydown', onEscape, true);
  };

  return { item, sep, label, submenu, place, openAnchored, openNodes, openAt, close, anchor: () => anchorBtn };
};
