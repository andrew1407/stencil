// Right-click probe on <all_urls> (also injected into open tabs by the SW): resolves what
// Stencil can grab under the cursor (ctxResolve.js) and messages the SW. Real <img>/<svg><image>
// return null (native context covers). Classic script — no import; orphan.js's claim stops a double
// injection and stands down a copy a reload orphaned.
(() => {
  const R = window.__stencilCtxResolve;
  const life = R && window.__stencilOrphan?.('__stencilCtxProbe', document, () => clearTimeout(dwell));
  if (!life) return;
  const { on } = life;
  const { resolveTarget, rememberPoster } = R;

  // mirror of lib/messages.js (classic content script — can't import)
  const MSG = { WAKE: 'stencil-wake', CTX: 'stencil-ctx' };

  // Wake the lazy SW so the menu exists before the first right-click.
  try {
    chrome.runtime.sendMessage({ type: MSG.WAKE }, () => void chrome.runtime.lastError);
  } catch {
    /* ignore */
  }
  // Stamp posters at load and as playback begins: a capture listener runs before the player's own.
  try {
    document.querySelectorAll('video').forEach(rememberPoster);
  } catch {
    /* ignore */
  }
  on('play', (e) => rememberPoster(e.target), true);

  // The SW may be asleep / the page navigating — a failed send is fine.
  const send = (data, x, y) => {
    try {
      chrome.runtime.sendMessage({ type: MSG.CTX, data, point: { x, y } });
    } catch {
      /* ignore */
    }
  };

  // An update sent from `contextmenu` loses the race with Chrome's menu (on macOS it fires in the
  // press's own task), so a pointer that RESTS resolves ahead of the click; motion costs a timer reset.
  const DWELL_MS = 150;
  let dwell = null;
  let primedKey = '';
  // Dedupe key: resting on ten tiles of the same background sends one message.
  const keyOf = (d) => (!d ? '' : `${d.video ? 'v' : 'i'}|${d.url || ''}|${d.imgUrl || ''}|${d.poster || ''}`);
  const report = (target, x, y, light) => {
    let data = null;
    try { data = resolveTarget(target, x, y, light); } catch { data = null; }
    const key = keyOf(data);
    if (light && key === primedKey) return;                      // nothing changed for the menu
    primedKey = key;
    send(data, x, y);
  };
  const arm = (e) => {
    clearTimeout(dwell);
    const { target, clientX: x, clientY: y } = e;
    dwell = setTimeout(() => { dwell = null; report(target, x, y, true); }, DWELL_MS);
  };
  on('pointerover', arm, true);
  // The pointer may already sit inside an element when this document_idle script arrives.
  on('pointermove', arm, { capture: true, once: true });
  // A right-BUTTON press precedes `contextmenu` on every platform — one last chance to prime.
  on('mousedown', (e) => {
    if (e.button !== 2) return;
    clearTimeout(dwell);
    report(e.target, e.clientX, e.clientY, true);
  }, true);

  // The authoritative resolve: frame capture included, with the exact point for the SW's re-capture.
  on('contextmenu', (e) => {
    clearTimeout(dwell);
    report(e.target, e.clientX, e.clientY, false);
  }, true);
})();
