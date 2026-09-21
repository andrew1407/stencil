// Clones of the live controls and coord panel for the fullscreen panels. Display only:
// every interaction relays to the original, whose own change keeps the clone in step.
export const populateFsControls = (fsControlsPanel) => {
  const existing = fsControlsPanel.querySelector('.controls');
  if (existing) existing.remove();
  const src = document.querySelector('#controls-body .controls');
  if (src) {
    const clone = src.cloneNode(true);
    fsControlsPanel.appendChild(clone);
    bindClonedControls(fsControlsPanel, src);
  }
};

export const bindClonedControls = (cloneRoot, srcRoot) => {
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
    if (kind === 'checkbox') srcEl.addEventListener('change', () => { cloneEl.checked = srcEl.checked; });
    else if (kind === 'select') srcEl.addEventListener('change', () => { cloneEl.value = srcEl.value; });
  });
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
    clone.classList.remove('coord-collapsed', 'coord-folding');
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
  }
};
