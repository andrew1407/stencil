export function wireFormulaControls(app) {
  document.getElementById('allow-formulas').addEventListener('change', e => {
    e.target.closest('.pill-toggle')?.classList.toggle('on', e.target.checked);
    app.settings.setAllowFormulas(e.target.checked);
  });
  // The debounce and the "don't judge a half-written expression" rule live in
  // settingsController; the context-menu twins go through the same call.
  app.settings.wireFormulaInputs({
    x: 'formula-x', y: 'formula-y', mirrorX: 'ctx-formula-x', mirrorY: 'ctx-formula-y',
  });
}
