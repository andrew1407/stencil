import { popoverPosition, wireModalOpenGestures } from './popover.js';
import { surfaceIn, surfaceOut, settleSurface, SURFACE_OUT_MS, motionReduced } from './motion.js';
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

// ONE Escape listener for every shell, rather than one per shell arbitrating with the
// rest: the TOPMOST open window answers, and only it, so a single press can never take a
// stacked window and the one it was raised from together. A shell wired with
// `escapeClose: false` (settingsModal) keeps its FULL modal open — its own capture-phase
// listener owns Escape while a hotkey is being rebound — though the popover shape always
// closes. Registered on the first wired shell, where the per-shell listeners used to go.
let escapeWired = false;
const wireEscapeOnce = () => {
  if (escapeWired) return;
  escapeWired = true;
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const open = [...modalShells].filter((s) => s.isOpen());
    if (!open.length) return;
    // A stacked window sits OVER whatever raised it, so it is the one on top.
    const top = open.find((s) => s.stacked) || open[0];
    if (top.takesEscape()) top.close();
  });
};

// Close whatever full/popover modal is open (optionally sparing one). Returns the first
// shell closed, so callers can tell "I replaced something" from "nothing was open". Every
// open shell goes: a `stacked` window means two can be up at once.
export const closeOpenModal = (except = null) => {
  let closed = null;
  for (const shell of modalShells) {
    if (shell === except || !shell.isOpen()) continue;
    shell.close();
    closed = closed || shell;
  }
  return closed;
};

// ── Grow-from-the-icon motion, on its own (js/ui/motion.js surfaceIn/surfaceOut) ──
// A window forms from motes streaming out of the control that opened it, and comes
// apart into motes pouring back into it. Extracted from the shell below because the
// CONFIRM dialog needs the same flight and has no opener button to hang one off: it is
// raised by whatever the user just did, so its origin is that gesture's own point.
// The `--modal-*` vars stay — they are the flight modalFromIcon/modalToIcon plays
// wherever the dust declines (an unmeasurable box, a stub, reduced motion).
export const MODAL_CLOSE_MS = SURFACE_OUT_MS;   // the dust's own clock (css/animations.css)
export const createModalFlight = (overlay, boxOf) => {
  let closeTimer = null;
  let originPoint = null;   // the origin centre, in client coordinates
  // The shared gate (ui/motionPrefs.js): the OS preference OR the user's 'none' mode.
  const reducedMotion = () => motionReduced();

  // `anchor` is a client rect (an icon's, or a small box around a click) or null — a
  // hidden opener measures 0x0 and a scrolled-away one sits outside the viewport, and
  // both fall from above instead.
  const setOrigin = (anchor) => {
    const box = boxOf();
    if (!box) return false;
    // Measure with the animation suppressed: `both` fill means the box already wears the
    // from-state transform, so an unguarded rect feeds our own offsets back in.
    overlay.classList.add('modal-measuring');
    const b = box.getBoundingClientRect();
    overlay.classList.remove('modal-measuring');
    if (!b.width || !b.height) return false;
    const a = anchor;
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
  // The close half, as every caller plays it: measure while the window is still up,
  // hand over to the cloud, and wear `modal-closing` for exactly the flight.
  const playClosing = () => {
    overlay.classList.add('modal-closing');
    playDust(false);
    if (closeTimer) clearTimeout(closeTimer);
    closeTimer = setTimeout(() => { closeTimer = null; finishClose(); }, MODAL_CLOSE_MS);
  };
  return { reducedMotion, setOrigin, finishClose, playDust, playClosing,
           settle: () => settleSurface(boxOf()) };
};

// `originEl` resolves the control the flight belongs to when it isn't the opener button
// (a gear inside a popup menu is already hidden, so its rect is 0x0). Null is a valid
// answer: nothing on screen owns the window, and falling from above is then correct.
// `stacked` shells open OVER what is showing instead of replacing it — closing the
// projects list to ask about one of its rows lost the user their place.
export const wireModalShell = (overlay, openBtn, closeBtn, { onOpen, onClose, escapeClose = true, originEl: originFor = null, stacked = false } = {}) => {
  // The modal box the popover positions: the shared class, or a shell with its own ids
  // (settingsModal) falls back to the overlay's first element child.
  const boxOf = () => overlay.querySelector('.app-modal') || overlay.firstElementChild;

  // The window's flight, shared with the confirm dialog (createModalFlight above).
  // Which control it belongs to defaults to the shell's own opener, but a caller can
  // pass another (the idle canvas's "＋ Blank image" card opens the SAME dialog and must
  // grow out of itself, not out of the toolbar icon).
  const flight = createModalFlight(overlay, boxOf);
  const { reducedMotion, finishClose, playDust } = flight;
  let originEl = openBtn;
  // Where the close flies back to, when that isn't where the window came from: a
  // context-menu row is gone by then, so the dust pours into the "⋯" that opened the
  // menu. An element, not a rect — it is measured at close time.
  let closeOriginEl = null;
  // Whether THIS window is currently stacked; the wire-time `stacked` is the default. One
  // shell serves two callers — Open-in replaces from the toolbar, stacks from a row.
  let stackedNow = stacked;
  const defaultOrigin = () => (originFor ? originFor() : openBtn);
  // An anchor is an element OR a plain client rect — a caller whose control is about to
  // hide (a gear inside a closing popup) captures the rect and passes it directly.
  const anchorLike = (v) => !!v && (typeof v.getBoundingClientRect === 'function' || Number.isFinite(v.width));
  const rectOf = (el) => (typeof el?.getBoundingClientRect === 'function'
    ? el.getBoundingClientRect()
    : (Number.isFinite(el?.width) ? el : null));
  const setOriginVars = (el = originEl) => flight.setOrigin(rectOf(el));
  const onScreenRect = (r) =>
    !!r && r.width > 0 && r.height > 0 && r.bottom > 0 && r.top < (window.innerHeight || 0);
  // Where a window collapses to when the control it belongs to is NOT on screen any more:
  // the canvas, which is what the user is looking at. Falling off the top is the ENTRANCE
  // for a window nobody asked for; as an exit it leaves towards nothing, and the openers
  // this happens to are the ones the window's own work hides (#load-image-btn goes the
  // moment an image exists). A small box, so the shrink reads as collapsing INTO it.
  const CLOSE_HOME_PX = 40;
  const canvasHomeRect = () => {
    const r = document.getElementById('canvas-viewport')?.getBoundingClientRect?.();
    const cx = r && r.width ? r.left + r.width / 2 : (window.innerWidth || 0) / 2;
    const cy = r && r.height ? r.top + r.height / 2 : (window.innerHeight || 0) / 2;
    const half = CLOSE_HOME_PX / 2;
    return { left: cx - half, top: cy - half, right: cx + half, bottom: cy + half,
             width: CLOSE_HOME_PX, height: CLOSE_HOME_PX };
  };

  // `from` is the control the flight belongs to. An explicit `null` means there ISN'T
  // one: the window falls from above rather than claiming a gesture that never
  // happened. Omitting it still means "the shell's own opener".
  const open = (from, backTo = null, { stacked: stackThisOpen = stacked } = {}) => {
    originEl = anchorLike(from) ? from
             : (from === null ? null : defaultOrigin());
    closeOriginEl = anchorLike(backTo) ? backTo : null;
    stackedNow = stackThisOpen;
    if (!stackedNow) closeOpenModal(api);   // one window at a time: the new one replaces whatever was showing
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
    // An open with no gesture behind it (originEl null — the projects modal's on-boot
    // auto-chooser opens itself this way) still has a home to shrink BACK into: the
    // control that would reopen it. Falls from above again only if that's off-screen too.
    const home = closeOriginEl || originEl || defaultOrigin();
    const animate = overlay.classList.contains('modal-open') && !reducedMotion()
      && setOriginVars(onScreenRect(rectOf(home)) ? home : canvasHomeRect());
    overlay.classList.remove('modal-open');
    if (animate) {
      flight.playClosing();   // measured above, while it was still open
    } else {
      flight.settle();   // nothing plays: drop anything an open left in the air
      finishClose();
    }
    // However it closed (X button, Escape, outside click), the gesture machine
    // must not keep thinking its popover is showing — a leaked 'sticky' mode
    // would let a later Alt glide close a FULL modal the user opened.
    gestures?.notifyClosed();
  };
  const openPopover = (anchorEl) => {
    if (overlay.classList.contains('modal-open')) return;   // already showing, either shape
    if (!stacked) closeOpenModal(api);
    finishClose();
    onOpen?.();
    // A popover grows from what it is anchored to — unless that control is hidden (a
    // gear inside the menu that just closed), when the shell's resolver knows better.
    const ar = anchorEl?.getBoundingClientRect?.();
    originEl = (ar && ar.width > 0 && ar.height > 0) ? anchorEl : defaultOrigin();
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
  const api = { open, close, openPopover, toggle, isOpen: () => overlay.classList.contains('modal-open'),
                get stacked() { return stackedNow; },
                takesEscape: () => escapeClose || overlay.classList.contains('modal-popover') };
  modalShells.add(api);
  wireEscapeOnce();
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
    // Did the press land in a layer above this window? Everything a window raises stacks
    // over it, so read the answer off the z-order rather than a class list that goes stale.
    const pressedAboveBox = (target) => {
      const mine = parseInt(getComputedStyle(overlay).zIndex, 10);
      if (!Number.isFinite(mine)) return false;
      for (let el = target; el && el !== document.body; el = el.parentElement) {
        const z = parseInt(getComputedStyle(el).zIndex, 10);
        if (Number.isFinite(z) && z >= mine) return true;
      }
      return false;
    };
    // POPOVER shape: the overlay is pointer-transparent (CSS) so an Alt glide reaches the
    // other toolbar icons, and click-outside dismissal moves here instead. A press outside
    // the box closes and is swallowed — unless it landed in something the popover raised
    // (its row menu lives on <body>, and dismissing on that killed the editor it opened).
    document.addEventListener('pointerdown', (e) => {
      if (!overlay.classList.contains('modal-open') || !overlay.classList.contains('modal-popover')) return;
      const box = boxOf();
      if (!box || box.contains(e.target) || pressedAboveBox(e.target)) return;
      e.preventDefault();
      e.stopPropagation();
      close();
    }, true);
  }
  if (closeBtn) closeBtn.addEventListener('click', close);
  overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
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
