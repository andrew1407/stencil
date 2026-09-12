export function wireFormulaControls(app) {
  document.getElementById('allow-formulas').addEventListener('change', e => {
    e.target.closest('.pill-toggle')?.classList.toggle('on', e.target.checked);
    app.settings.setAllowFormulas(e.target.checked);
  });
  // Typing settles → the formula applies (mirrored into the context-menu twins). The
  // debounce and the "don't judge a half-written expression" rule live in the shared
  // wiring; the context menu's pair goes through the same call — see settingsController.
  app.settings.wireFormulaInputs({
    x: 'formula-x', y: 'formula-y', mirrorX: 'ctx-formula-x', mirrorY: 'ctx-formula-y',
  });
}
