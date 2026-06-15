// ── In-page highlight overlay (injected) ─────────────────────────────────────
// toggleStencilHighlight(on) marks every element Stencil can pull an image from
// — <img>, inline <svg><image>, and elements with a CSS background-image (incl.
// ::before / ::after) — with a persistent outline, AND:
//   • tracks the cursor so the element under the mouse gets a stronger highlight;
//   • watches the DOM (MutationObserver) so content added later — e.g. images
//     that lazy-load as you scroll — gets outlined too.
// Toggling off (or re-running with on=false) removes outlines, the hover style,
// the mouse listener and the observer. Injected via chrome.scripting into the
// PAGE (isolated world); must be self-contained — no imports, no module-scope
// refs. Teardown is stashed on window so a later call can detach everything.
// Returns the number of statically highlighted elements.
export const toggleStencilHighlight = (on) => {
  const STYLE_ID = 'stencil-hl-style';
  const ATTR = 'data-stencil-hl';          // statically marked, Stencil-grabbable
  const HOVER = 'data-stencil-hl-hover';   // the one currently under the cursor

  // Tear down any previous run first (idempotent across re-injection).
  if (typeof window.__stencilHlCleanup === 'function') {
    window.__stencilHlCleanup();
    window.__stencilHlCleanup = null;
  }
  document.querySelectorAll('[' + ATTR + ']').forEach(el => el.removeAttribute(ATTR));
  document.querySelectorAll('[' + HOVER + ']').forEach(el => el.removeAttribute(HOVER));
  const prevStyle = document.getElementById(STYLE_ID);
  if (prevStyle) prevStyle.remove();
  if (!on) return 0;

  const hasBgImage = (el) => {
    for (const pseudo of [null, '::before', '::after']) {
      const bg = getComputedStyle(el, pseudo).backgroundImage;
      if (!bg || bg === 'none') continue;
      const re = /url\((['"]?)(.*?)\1\)/g;
      let m;
      while ((m = re.exec(bg))) {
        if (m[2] && !/^data:image\/svg/i.test(m[2])) return true;
      }
    }
    return false;
  };
  const isImageEl = (el) => !!(el.matches && (el.matches('img') || el.matches('image') || el.matches('video')));
  const grabbable = (el) => el.nodeType === 1 && (isImageEl(el) || hasBgImage(el));
  // Nearest grabbable element at/above `start` — follows the cursor even when
  // it's over a child of a background element.
  const grabbableAt = (start) => {
    for (let n = start; n && n.nodeType === 1; n = n.parentElement) {
      if (grabbable(n)) return n;
    }
    return null;
  };

  // Mark one element (and its descendants) if grabbable. Used for the initial
  // pass and for nodes the observer reports later.
  const markWithin = (root) => {
    if (!root || root.nodeType !== 1) return;
    if (grabbable(root)) root.setAttribute(ATTR, '');
    if (root.querySelectorAll) {
      for (const el of root.querySelectorAll('*')) {
        if (!el.hasAttribute(ATTR) && grabbable(el)) el.setAttribute(ATTR, '');
      }
    }
  };
  markWithin(document.body || document.documentElement);

  const style = document.createElement('style');
  style.id = STYLE_ID;
  style.textContent =
    '[' + ATTR + ']{outline:2px solid #7c3aed !important;outline-offset:-2px !important;}' +
    '[' + HOVER + ']{outline:3px solid #a855f7 !important;outline-offset:-3px !important;' +
    'box-shadow:0 0 0 3px rgba(168,85,247,.45) !important;}';
  (document.head || document.documentElement).appendChild(style);

  // Cursor tracking: move the HOVER marker to the grabbable element under the mouse.
  let current = null;
  const onOver = (e) => {
    const target = grabbableAt(e.target);
    if (target === current) return;
    if (current) current.removeAttribute(HOVER);
    current = target;
    if (current) current.setAttribute(HOVER, '');
  };
  document.addEventListener('mouseover', onOver, true);

  // Keep up with the page: outline nodes added later (lazy images on scroll,
  // SPA route changes) and re-evaluate elements whose style/class just changed
  // (a class can add or remove a background-image).
  const observer = new MutationObserver((mutations) => {
    for (const mu of mutations) {
      if (mu.type === 'childList') {
        mu.addedNodes.forEach(markWithin);
      } else if (mu.type === 'attributes') {
        const el = mu.target;
        if (el.nodeType !== 1 || el.id === STYLE_ID) continue;
        if (grabbable(el)) el.setAttribute(ATTR, '');
        else el.removeAttribute(ATTR);
      }
    }
  });
  observer.observe(document.documentElement, {
    childList: true, subtree: true, attributes: true, attributeFilter: ['style', 'class', 'src', 'srcset']
  });

  window.__stencilHlCleanup = () => {
    document.removeEventListener('mouseover', onOver, true);
    observer.disconnect();
    if (current) current.removeAttribute(HOVER);
    current = null;
  };

  return document.querySelectorAll('[' + ATTR + ']').length;
};
