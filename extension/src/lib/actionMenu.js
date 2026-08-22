// ── The floating action menu (the rows' ⋯ menu) ─────────────────────────────
// One shared menu element per surface: item/sep/label builders, a hover-flyout submenu,
// flip-and-clamp placement with the pop animation's transform origin, anchored open
// (toggling, marking the anchor .active) and point open (right-click), and a
// capture-phase Escape armed only while the menu is open. Placement math is pure and
// exported for tests; the factory takes its DOM (menuEl, doc, win) and the caller's
// action runner, so popup.js and editorMode.js build their contents from the SAME pieces.
import { menuTransformOrigin } from './chatMsgMenu.js';
import { icon } from './icons.js';

const MARGIN = 6;   // minimum distance to a viewport edge

/**
 * Top-left placement for the menu at (x, y): pull back inside the viewport when it
 * would overflow the right/bottom edge, and never closer than MARGIN to any edge.
 */
export const menuPlacement = ({ x, y, size, viewport }) => {
  if (x + size.width > viewport.width) x = Math.max(MARGIN, viewport.width - size.width - MARGIN);
  if (y + size.height > viewport.height) y = Math.max(MARGIN, viewport.height - size.height - MARGIN);
  return { left: Math.max(MARGIN, x), top: Math.max(MARGIN, y) };
};

/** X for a menu beside an anchor button: prefer its left, fall back to its right. */
export const anchoredX = ({ rect, width }) => {
  const x = rect.left - width - MARGIN;
  return x < MARGIN ? rect.right + MARGIN : x;
};

/**
 * Submenu flyout placement, computed in viewport space (open right of the head, flip
 * left on overflow, hard-clamp both axes) and returned wrap-relative — the flyout is
 * position:absolute in its .submenu wrap, so a clamped target must be converted.
 */
export const flyoutPlacement = ({ head, wrap, size, viewport }) => {
  let vLeft = head.right - 2;                       // prefer opening to the right of the head
  if (vLeft + size.width > viewport.width - MARGIN) vLeft = head.left - size.width + 2;
  vLeft = Math.max(MARGIN, Math.min(vLeft, viewport.width - size.width - MARGIN));
  const vTop = Math.max(MARGIN, Math.min(head.top - 5, viewport.height - size.height - MARGIN));
  return { left: vLeft - wrap.left, top: vTop - wrap.top };
};

/**
 * Build the shared action-menu controller around one (already-styled) menu element.
 *
 * @param {object} deps
 * @param {HTMLElement} deps.menuEl - The single #action-menu element.
 * @param {(fn: Function) => any} [deps.run] - Runs a clicked item's action (the caller's
 *   error-reporting wrapper). Defaults to calling the action directly.
 * @param {Document} [deps.doc]
 * @param {Window} [deps.win]
 */
export const createActionMenu = ({ menuEl, run = (fn) => fn(), doc = document, win = window }) => {
  let anchorBtn = null;

  // The label is appended as a TEXT NODE, never innerHTML: callers feed this
  // page-derived names (project names), which must stay data.
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

  // Non-clickable sub-category heading.
  const label = (text) => {
    const d = doc.createElement('div');
    d.className = 'label';
    d.textContent = text;
    return d;
  };

  // A nested submenu shown as a flyout on hover. CSS :hover controls its VISIBILITY;
  // the JS only repositions it — absolute in its .submenu wrap, NOT fixed, because the
  // menu's entrance animation leaves a transform matrix a fixed child would resolve against.
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
      const wr = wrap.getBoundingClientRect();      // abs-positioning origin (.submenu is position:relative)
      fly.style.margin = '0';
      // :hover already made it display:block, so its size is measurable.
      const p = flyoutPlacement({
        head: hr, wrap: wr,
        size: { width: fly.offsetWidth, height: fly.offsetHeight },
        viewport: { width: win.innerWidth, height: win.innerHeight },
      });
      fly.style.left = `${p.left}px`;
      fly.style.top = `${p.top}px`;
    };
    wrap.addEventListener('mouseenter', place);
    wrap.append(head, fly);
    return wrap;
  };

  // Place the (already-built, visible) menu at top-left x/y, flipping to stay on-screen.
  // `origin` is the click point the pop animation grows from (defaults to x/y).
  const place = (x, y, origin = { x, y }) => {
    const size = { width: menuEl.offsetWidth, height: menuEl.offsetHeight };
    const { left, top } = menuPlacement({
      x, y, size, viewport: { width: win.innerWidth, height: win.innerHeight },
    });
    menuEl.style.left = `${left}px`;
    menuEl.style.top = `${top}px`;
    menuEl.style.transformOrigin = menuTransformOrigin({ x: origin.x, y: origin.y, left, top, size });
  };

  // Escape closes only the open menu: capture-phase so it wins over the surface's own
  // handlers, armed on open and disarmed on close (never leaks, and never intercepts
  // while another owner drives the shared element).
  const onEscape = (e) => {
    if (e.key !== 'Escape') return;
    e.preventDefault();
    e.stopPropagation();
    close();
  };
  const armEscape = () => doc.addEventListener('keydown', onEscape, true);

  // Open anchored to a button (toggles closed if already open on it): mark the anchor
  // BEFORE `fill` builds the contents (fill may capture it), then place beside the
  // button — preferring its left, falling back to its right if it won't fit.
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

  // Anchored open with caller-built items — editor mode's per-row menu. Same element,
  // placement and styling; only the contents differ.
  const openNodes = (btn, nodes) => openAnchored(btn, () => {
    menuEl.innerHTML = '';
    menuEl.append(...nodes);
  });

  // Open at a point (row right-click); no button is anchored.
  const openAt = (x, y, fill) => {
    close();
    fill();
    menuEl.hidden = false;
    place(x, y);
    armEscape();
  };

  const close = () => {
    menuEl.hidden = true;
    menuEl.innerHTML = '';
    if (anchorBtn) anchorBtn.classList.remove('active');
    anchorBtn = null;
    doc.removeEventListener('keydown', onEscape, true);
  };

  return { item, sep, label, submenu, place, openAnchored, openNodes, openAt, close, anchor: () => anchorBtn };
};
