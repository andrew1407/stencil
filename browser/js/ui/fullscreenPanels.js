// ── The two slide-in panels: show, hide-after-a-grace, and the selection top ─
// Owns both auto-hide timers, so the hover wiring and the resizer drag pause them through
// `pauseControlsHide` / `pausePointsHide` instead of reaching for a shared variable.
export const createFsPanels = ({ fsControlsPanel, fsPointsPanel, showPoints }) => {
  let controlsHideTimer = null;
  let pointsHideTimer = null;

  // ── Panel show/hide helpers ──
  const updateFsSelectionTop = ctrlsVisible => {
    const fsSel = document.getElementById('fs-selection-panel');
    if (!fsSel || fsSel.style.display === 'none') return;
    if (ctrlsVisible) {
      // Use offsetHeight — not getBoundingClientRect — so transform doesn't affect measurement
      fsSel.style.top = fsControlsPanel.offsetHeight + 'px';
    } else {
      fsSel.style.top = '0px';
    }
    // Keep trigger zone covering the selection panel
    requestAnimationFrame(() => {
      const trigger = document.getElementById('fs-top-trigger');
      if (trigger) trigger.style.height = Math.max(8, fsSel.getBoundingClientRect().bottom) + 'px';
    });
  };

  const showControlsPanel = () => {
    clearTimeout(controlsHideTimer);
    // Update selection panel top BEFORE adding class so CSS transitions start together
    updateFsSelectionTop(true);
    fsControlsPanel.classList.add('fs-panel-visible');
  };
  const hideControlsPanel = () => {
    clearTimeout(controlsHideTimer);
    controlsHideTimer = setTimeout(() => {
      // Update selection panel top BEFORE removing class
      updateFsSelectionTop(false);
      fsControlsPanel.classList.remove('fs-panel-visible');
      // After CSS transition (0.25s), update trigger height
      setTimeout(() => {
        const fsSel = document.getElementById('fs-selection-panel');
        const trigger = document.getElementById('fs-top-trigger');
        if (fsSel && trigger && fsSel.style.display !== 'none')
          trigger.style.height = Math.max(8, fsSel.getBoundingClientRect().bottom) + 'px';
      }, 260);
    }, 400);
  };
  const showPointsPanel = () => {
    clearTimeout(pointsHideTimer);
    showPoints();
    fsPointsPanel.classList.add('fs-panel-visible');
  };
  const hidePointsPanel = () => {
    clearTimeout(pointsHideTimer);
    pointsHideTimer = setTimeout(() => {
      fsPointsPanel.classList.remove('fs-panel-visible');
    }, 400);
  };

  return {
    showControlsPanel, hideControlsPanel, showPointsPanel, hidePointsPanel,
    pauseControlsHide: () => clearTimeout(controlsHideTimer),
    pausePointsHide: () => clearTimeout(pointsHideTimer),
  };
};
