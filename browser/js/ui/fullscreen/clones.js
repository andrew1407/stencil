// Clones of the live controls and coord panel for the fullscreen panels. Display only:
// every interaction relays to the original, whose own change keeps the clone in step.

// Each strip's mirror listeners on the live controls, dropped before its next copy is bound.
const unbinds = new WeakMap();

export const populateFsControls = (fsControlsPanel) => {
  const existing = fsControlsPanel.querySelector('.controls');
  if (existing) existing.remove();
  unbinds.get(fsControlsPanel)?.();
  unbinds.delete(fsControlsPanel);
  const src = document.querySelector('#controls-body .controls');
  if (src) {
    const clone = src.cloneNode(true);
    fsControlsPanel.appendChild(clone);
    unbinds.set(fsControlsPanel, bindClonedControls(fsControlsPanel, src));
  }
};

export const bindClonedControls = (cloneRoot, srcRoot) => {
  const offs = [];
  srcRoot.querySelectorAll('[id]').forEach(srcEl => {
    const cloneEl = cloneRoot.querySelector('#' + srcEl.id);
    if (!cloneEl) return;
    if (cloneEl.tagName === 'BUTTON') {
      cloneEl.addEventListener('click', () => srcEl.click());
      return;
    }
    const kind = cloneEl.tagName === 'SELECT' ? 'select' : cloneEl.type;
    if (!['checkbox', 'color', 'number', 'file', 'select'].includes(kind)) return;

    cloneEl.addEventListener(kind === 'color' ? 'input' : 'change', () => {
      if (kind === 'checkbox') {
        srcEl.checked = cloneEl.checked;
      } else if (kind === 'file') {
        const dt = new DataTransfer();
        [...cloneEl.files].forEach(f => dt.items.add(f));
        srcEl.files = dt.files;
      } else {
        srcEl.value = cloneEl.value;
      }
      srcEl.dispatchEvent(new Event('change', { bubbles: true }));
    });
    const mirror = kind === 'checkbox' ? () => { cloneEl.checked = srcEl.checked; }
      : kind === 'select' ? () => { cloneEl.value = srcEl.value; } : null;
    if (!mirror) return;
    srcEl.addEventListener('change', mirror);
    offs.push(() => srcEl.removeEventListener('change', mirror));
  });
  return () => { for (const off of offs) off(); };
};

export const populateFsPoints = (fsPointsPanel) => {
  fsPointsPanel.innerHTML = '';
  const src = document.getElementById('coord-panel');
  if (src) {
    const clone = src.cloneNode(true);
    clone.id = 'fs-coord-panel-clone';
    clone.querySelectorAll('[id]').forEach(el => {
      el.id = 'fs-clone-' + el.id;
    });
    clone.classList.remove('coord-collapsed', 'coord-folding', 'coord-veiled', 'surface-leaving', 'surface-forming');
    clone.style.minWidth = '0';
    clone.style.maxWidth = '100%';
    clone.style.marginTop = '0';
    clone.style.background = 'transparent';
    // The hover panel hides on its own: no collapse chevron. A tab click switches the ORIGINAL
    // and re-clones, since the observer that refreshes the copy watches the points body only.
    clone.querySelector('#fs-clone-toggle-coord-panel')?.remove();
    for (const tab of ['coord-tab-points', 'coord-tab-lines']) {
      clone.querySelector('#fs-clone-' + tab)?.addEventListener('click', () => {
        document.getElementById(tab)?.click();
        populateFsPoints(fsPointsPanel);
      });
    }
    fsPointsPanel.appendChild(clone);
    // A windowed table (panel/rowWindow.js) holds the rows its scroller shows: the copy opens on them.
    const from = src.querySelector('#coord-body');
    const to = clone.querySelector('#fs-clone-coord-body');
    if (from && to && src.querySelector('.row-window-spacer')) to.scrollTop = from.scrollTop;
  }
};

// The mirror re-clones once a frame while `isOn()`, after a change to either tab's rows in `body`:
// the points table moves per frame while drawing, the Lines tab on every line edit.
export const followCoordBody = (body, isOn, refresh) => {
  if (!body || typeof MutationObserver === 'undefined') return;
  let raf = 0;
  new MutationObserver(() => {
    if (!isOn() || raf) return;
    raf = requestAnimationFrame(() => {
      raf = 0;
      if (isOn()) refresh();
    });
  }).observe(body, { childList: true, subtree: true, characterData: true });
};
