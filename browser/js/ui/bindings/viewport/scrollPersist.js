export function wireScrollPersist(app) {
  // Save scroll position (debounced) so it's restored on reopen
  {
    const scrollVp = document.getElementById('canvas-viewport');
    if (scrollVp) {
      let scrollSaveTimer = null;
      scrollVp.addEventListener('scroll', () => {
        clearTimeout(scrollSaveTimer);
        scrollSaveTimer = setTimeout(() => app.storage.save(), 400);
      });
    }
  }
}
