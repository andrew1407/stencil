// ── Cloning the live controls and coord panel into the fullscreen panels ────
// The clones are display only: every interaction relays back to the original control, and
// the original's own change keeps the clone in step, so state lives in exactly one place.
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

// The clone is display only — every interaction relays to the original controls.
export const bindClonedControls = (cloneRoot, srcRoot) => {
  srcRoot.querySelectorAll('[id]').forEach(srcEl => {
    const cloneEl = cloneRoot.querySelector('#' + srcEl.id);
    if (!cloneEl) return;
    if (cloneEl.tagName === 'BUTTON') {
      cloneEl.addEventListener('click', () => srcEl.click());
      return;
    }
    // Mirror only the input kinds the fs panel uses; ignore others (radio/text).
    const kind = cloneEl.tagName === 'SELECT' ? 'select' : cloneEl.type;
    if (!['checkbox', 'color', 'number', 'file', 'select'].includes(kind)) return;

    // Relay clone → original: copy the relevant property, then fire change.
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
    // Keep the clone in sync when the original changes (checkbox + select).
    if (kind === 'checkbox') srcEl.addEventListener('change', () => { cloneEl.checked = srcEl.checked; });
    else if (kind === 'select') srcEl.addEventListener('change', () => { cloneEl.value = srcEl.value; });
  });
};

export const populateFsPoints = (fsPointsPanel) => {
  fsPointsPanel.innerHTML = '';
  const src = document.getElementById('coord-panel');
  if (src) {
    const clone = src.cloneNode(true);
    // Give cloned elements new ids to avoid conflicts
    clone.id = 'fs-coord-panel-clone';
    clone.querySelectorAll('[id]').forEach(el => {
      el.id = 'fs-clone-' + el.id;
    });
    clone.classList.remove('coord-collapsed', 'coord-folding');
    clone.style.minWidth = '0';
    clone.style.maxWidth = '100%';
    clone.style.marginTop = '0';
    clone.style.background = 'transparent';
    fsPointsPanel.appendChild(clone);
  }
};
