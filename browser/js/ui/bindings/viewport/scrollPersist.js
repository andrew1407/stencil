export function wireScrollPersist(app) {
  // Save scroll position (debounced) so it's restored on reopen
  {
    const scrollVp = document.getElementById('canvas-viewport');
    if (scrollVp) {
      scrollVp.addEventListener('scroll', () => app.storage.saveSoon());
    }
  }
}
