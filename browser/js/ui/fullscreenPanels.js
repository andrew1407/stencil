// The two slide-in panels and their auto-hide timers; the hover wiring and the resizer drag
// pause them through `pauseControlsHide` / `pausePointsHide`.
export const createFsPanels = ({ fsControlsPanel, fsPointsPanel, showPoints }) => {
  let controlsHideTimer = null;
  let pointsHideTimer = null;

  const updateFsSelectionTop = ctrlsVisible => {
    const fsSel = document.getElementById('fs-selection-panel');
    if (!fsSel || fsSel.style.display === 'none') return;
    if (ctrlsVisible) {
      // offsetHeight, not getBoundingClientRect: the transform must not affect the measure
      fsSel.style.top = fsControlsPanel.offsetHeight + 'px';
    } else {
      fsSel.style.top = '0px';
    }
    requestAnimationFrame(() => {
      const trigger = document.getElementById('fs-top-trigger');
      if (trigger) trigger.style.height = Math.max(8, fsSel.getBoundingClientRect().bottom) + 'px';
    });
  };

  const showControlsPanel = () => {
    clearTimeout(controlsHideTimer);
    // Before the class, so the CSS transitions start together
    updateFsSelectionTop(true);
    fsControlsPanel.classList.add('fs-panel-visible');
  };
  const hideControlsPanel = () => {
    clearTimeout(controlsHideTimer);
    controlsHideTimer = setTimeout(() => {
      updateFsSelectionTop(false);
      fsControlsPanel.classList.remove('fs-panel-visible');
      // After the 0.25s CSS transition
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
