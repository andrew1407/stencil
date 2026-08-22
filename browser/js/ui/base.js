import { popoverPosition, wireModalOpenGestures } from './popover.js';
import { surfaceIn, surfaceOut, settleSurface, SURFACE_OUT_MS } from './motion.js';
import { isTypingTarget } from '../utils.js';
// ── Web Component base: light-DOM custom elements ───────────────
// Each UI region owns its markup (static inner()) and behavior (wire(app)). Light
// DOM keeps global-id wiring, global CSS, and fullscreen cloneNode working. wire()
// waits for the one-shot `stencil:ready` (fired after DrawingApp exists) to preserve
// DOM → app → wire init order. Falls back to a plain base + no-op define() off-browser
// so the Node test runner (no DOM) can still import markup.
const ElementBase = typeof HTMLElement !== 'undefined' ? HTMLElement : class {};

// Register a custom element, but only in a browser (no customElements in Node).
export const define = (tag, klass) => {
  if (typeof customElements !== 'undefined') customElements.define(tag, klass);
};

export class StencilElement extends ElementBase {
  #wired = false;

  connectedCallback() {
    if (this.#wired) return;
    this.#wired = true;
    if (!this.firstElementChild && this.constructor.inner) this.innerHTML = this.constructor.inner();
    document.addEventListener('stencil:ready', e => this.wire(e.detail.app), { once: true });
  }

  // Overridden by subclasses that need behavior. `app` is the DrawingApp.
  wire(_app) {}
}

// Compose a host tag string for layout(): `<tag attrs>inner</tag>`.
export const hostTag = (tag, attrs, inner) => `<${tag}${attrs ? ' ' + attrs : ''}>${inner}</${tag}>`;

// Escape a string for safe interpolation into an innerHTML template. Use it for any
// value that can carry server-supplied or user-typed text (project names, server
// URLs/addresses) so a crafted value can't inject markup/script. Non-strings coerce.
export const escapeHtml = (v) => String(v == null ? '' : v)
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;')
  .replace(/'/g, '&#39;');

// ── Shared modal shell ──────────────────────────────────────────
// Wires open/close/overlay-mousedown/Escape for every app modal; returns
// { open, close, openPopover }. onOpen/onClose run BEFORE the modal-open class toggles.
// `escapeClose` false (settingsModal) suppresses the bubble-phase Escape so its own
// capture-phase listener can handle it. The open button answers the popover gestures
// (ui/popover.js): click opens the covering modal; dblclick / right-click / long press
// open the SAME modal as a compact popover pinned to the icon.
// Every wired modal shell, so opening one can close whichever other is showing.
const modalShells = new Set();

// Close whatever full/popover modal is currently open (optionally sparing one). Returns the
// shell that was closed, so callers can tell "I replaced something" from "nothing was open".
export const closeOpenModal = (except = null) => {
  for (const shell of modalShells) {
    if (shell === except || !shell.isOpen()) continue;
    shell.close();
    return shell;
  }
  return null;
};

export const wireModalShell = (overlay, openBtn, closeBtn, { onOpen, onClose, escapeClose = true } = {}) => {
  // The modal box the popover positions: the shared class, or a shell with its own ids
  // (settingsModal) falls back to the overlay's first element child.
  const boxOf = () => overlay.querySelector('.app-modal') || overlay.firstElementChild;

  // ── Grow-from-the-icon motion (js/ui/motion.js surfaceIn/surfaceOut) ──
  // The window forms from motes streaming out of the icon that opened it, and comes
  // apart into motes pouring back into it — same origin and direction the old scale
  // had. The `--modal-*` vars stay: they are the flight modalFromIcon/modalToIcon still
  // plays wherever the dust declines (an unmeasurable box, a stub, reduced motion).
  const CLOSE_MS = SURFACE_OUT_MS;   // the dust's own clock (css/animations.css)
  let closeTimer = null;
  let originPoint = null;   // the icon centre, in client coordinates
  const reducedMotion = () => !!window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  // Which control the flight belongs to. Defaults to the shell's own opener, but a
  // caller can pass another (the idle canvas's "＋ Blank image" card opens the SAME
  // dialog and must grow out of itself, not out of the toolbar icon).
  let originEl = openBtn;
  const setOriginVars = () => {
    const box = boxOf();
    if (!box) return false;
    // Measure with the animation suppressed: `both` fill means the box already wears the
    // from-state transform, so an unguarded rect feeds our own offsets back in.
    overlay.classList.add('modal-measuring');
    const b = box.getBoundingClientRect();
    overlay.classList.remove('modal-measuring');
    if (!b.width || !b.height) return false;
    const a = originEl?.getBoundingClientRect?.();
    // A hidden opener (collapsed Controls panel) measures 0x0 and a scrolled-away one
    // sits outside the viewport — both fall from above instead.
    const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
    const cx = onScreen ? a.left + a.width / 2 : b.left + b.width / 2;
    const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, b.height * 0.3);
    originPoint = { x: cx, y: cy };   // the dust streams out of / pours into this point
    box.style.setProperty('--modal-dx', `${Math.round(cx - (b.left + b.width / 2))}px`);
    box.style.setProperty('--modal-dy', `${Math.round(cy - (b.top + b.height / 2))}px`);
    // Floor the ratio so a big window doesn't animate from a sub-pixel speck.
    box.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / b.width, 0.05) : 0.4));
    box.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / b.height, 0.05) : 0.4));
    return true;
  };
  const clearOriginVars = () => {
    const box = boxOf();
    if (!box) return;
    for (const v of ['--modal-dx', '--modal-dy', '--modal-sx', '--modal-sy']) box.style.removeProperty(v);
  };
  // Drop the closing shape immediately (also called when a re-open interrupts it).
  const finishClose = () => {
    if (closeTimer) { clearTimeout(closeTimer); closeTimer = null; }
    overlay.classList.remove('modal-closing', 'modal-popover');
    const box = boxOf();
    if (box) { box.style.left = ''; box.style.top = ''; }
    clearOriginVars();
  };
  // Both flights start here, so a superseding open/close always drops the one in the
  // air (settleSurface) instead of leaving a cloud or a veiled box behind.
  const playDust = (enter) => {
    const box = boxOf();
    if (!box) return;
    if (reducedMotion()) { settleSurface(box); return; }
    (enter ? surfaceIn : surfaceOut)(box, originPoint);
  };

  // `from` is the control the flight belongs to. An explicit `null` means there ISN'T
  // one: the window falls from above rather than claiming a gesture that never
  // happened. Omitting it still means "the shell's own opener".
  const open = (from) => {
    originEl = (from && typeof from.getBoundingClientRect === 'function') ? from
             : (from === null ? null : openBtn);
    closeOpenModal(api);   // one window at a time: the new one replaces whatever was showing
    finishClose();
    onOpen?.();
    overlay.classList.add('modal-open');
    // Measured after the class applies — the box has no size while display:none.
    if (!reducedMotion() && setOriginVars()) playDust(true);
  };
  let gestures = null;   // set below when there is an opener button
  const close = () => {
    onClose?.();
    // `modal-open` is what every caller tests, so it comes off now; the shrink runs under
    // `modal-closing`, which is purely visual. Measure first — display:none measures 0.
    const animate = overlay.classList.contains('modal-open') && !reducedMotion() && setOriginVars();
    overlay.classList.remove('modal-open');
    if (animate) {
      overlay.classList.add('modal-closing');
      playDust(false);   // measured above, while it was still open
      if (closeTimer) clearTimeout(closeTimer);
      closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, CLOSE_MS);
    } else {
      settleSurface(boxOf());   // nothing plays: drop anything an open left in the air
      finishClose();
    }
    // However it closed (X button, Escape, outside click), the gesture machine
    // must not keep thinking its popover is showing — a leaked 'sticky' mode
    // would let a later Alt glide close a FULL modal the user opened.
    gestures?.notifyClosed();
  };
  const openPopover = (anchorEl) => {
    if (overlay.classList.contains('modal-open')) return;   // already showing, either shape
    closeOpenModal(api);
    finishClose();
    onOpen?.();
    overlay.classList.add('modal-open', 'modal-popover');
    const box = boxOf();
    if (box && anchorEl?.getBoundingClientRect) {
      // Measure AFTER the classes apply (the popover class caps the box's size).
      const p = popoverPosition({
        anchor: anchorEl.getBoundingClientRect(),
        box: box.getBoundingClientRect(),
        viewport: { width: window.innerWidth, height: window.innerHeight },
      });
      box.style.left = `${p.left}px`;
      box.style.top = `${p.top}px`;
    }
    // After pinning, so the popover grows from the icon toward where it actually lands.
    if (!reducedMotion() && setOriginVars()) playDust(true);
  };
  // Opening from the icon (or its shortcut) TOGGLES: pressing the same shortcut again, or
  // clicking the icon behind a popover, closes the window instead of re-opening it.
  const toggle = (from) => {
    if (overlay.classList.contains('modal-open')) { close(); return; }
    open(from);
  };
  const api = { open, close, openPopover, toggle, isOpen: () => overlay.classList.contains('modal-open') };
  modalShells.add(api);
  overlay.__stencilModal = api;
  if (openBtn) openBtn.__stencilModal = api;
  if (openBtn) {
    const g = gestures = wireModalOpenGestures(openBtn, {
      openFull: toggle,   // the icon (and its shortcut) closes the window it opened
      openPopover: () => openPopover(openBtn),
      // HOLD-to-peek support (popover.js): the machine only ever closes windows
      // it opened in a popover shape, so `close` never fires for the full modal.
      closePopover: close,
      isPopoverOpen: () => overlay.classList.contains('modal-open'),
      // Engaged at release time = the pointer rests inside the box (typed content
      // is holdLinger's job — auto-focused empty search fields must not count).
      isPeekEngaged: () => !!boxOf()?.matches(':hover'),
      // A lingering (engaged, released) peek is not closed by hover-out while the
      // user is mid-typing in one of its fields — "typing" meaning a text control
      // WITH CONTENT, since several modals auto-focus an empty search on open.
      holdLinger: () => {
        const a = document.activeElement;
        if (!a || !boxOf()?.contains(a) || !isTypingTarget(a)) return false;
        return String(a.value ?? '').trim() !== '';
      },
    });
    // The pointer crossing the box edge drives the linger close (machine boxLeave).
    const boxEl = boxOf();
    if (boxEl) {
      boxEl.addEventListener('mouseenter', () => g.boxEnter());
      boxEl.addEventListener('mouseleave', () => g.boxLeave());
    }
    // POPOVER shape: the page-covering overlay is pointer-TRANSPARENT (CSS), so an
    // Alt glide can reach the other toolbar icons. Click-outside dismissal moves
    // here: a press outside the box closes the popover and is swallowed.
    document.addEventListener('pointerdown', (e) => {
      if (!overlay.classList.contains('modal-open') || !overlay.classList.contains('modal-popover')) return;
      const box = boxOf();
      if (!box || box.contains(e.target)) return;
      e.preventDefault();
      e.stopPropagation();
      close();
    }, true);
  }
  if (closeBtn) closeBtn.addEventListener('click', close);
  overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
  // Escape closes every modal, EXCEPT `escapeClose: false` (settingsModal) keeps the
  // FULL modal open — its capture-phase listener owns Escape while rebinding a hotkey.
  // The POPOVER shape always closes; an in-progress capture stops propagation first.
  document.addEventListener('keydown', e => {
    if (e.key !== 'Escape' || !overlay.classList.contains('modal-open')) return;
    if (!escapeClose && !overlay.classList.contains('modal-popover')) return;
    close();
  });
  return api;
};

export const attachSearchFilter = (searchInput, applyFilterFn) => {
  searchInput.addEventListener('input', applyFilterFn);
};

// Pure per-row search predicate: empty/whitespace query matches everything;
// otherwise case-insensitive substring match. Trims the query internally so
// callers don't have to.
export const rowMatches = (text, query) => {
  const q = String(query ?? '').trim().toLowerCase();
  return !q || String(text ?? '').toLowerCase().includes(q);
};

// Populate a <select> with create targets: "Local" (value "") plus one option per
// connected server, showing/hiding its row. Shared by the create modals so console
// (stencil.blank/load { address }) and UI thread the same address (the parity rule).
// `allow` false (incognito) suppresses every server target — incognito content must
// not be created on a server.
export const fillTargetSelect = (selectEl, rowEl, connMgr, allow = true) => {
  const urls = (allow && connMgr) ? connMgr.urls : [];
  selectEl.innerHTML = '';
  const local = document.createElement('option');
  local.value = '';
  local.textContent = 'Local (this browser)';
  selectEl.appendChild(local);
  for (const url of urls) {
    const opt = document.createElement('option');
    opt.value = url;
    opt.textContent = url;
    selectEl.appendChild(opt);
  }
  if (rowEl) rowEl.style.display = urls.length ? '' : 'none';
  return urls.length > 0;
};
