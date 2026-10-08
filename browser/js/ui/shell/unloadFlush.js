// The page leaving or hiding: the trailing save, the co-edit result and the thumbnail land now.
// `pagehide` and a hidden `visibilitychange` cover what `beforeunload` misses (a mobile tab
// discarded in the background, the bfcache); each flush is a no-op when nothing is pending.
export const flushPendingWrites = (app) => {
  app.storage.saveSoon.flush();
  app.remoteSync.flushResult();
  app.storage.thumbs.flush();
};

export const installUnloadFlush = (app, win = window, doc = document) => {
  const flush = () => flushPendingWrites(app);
  win.addEventListener('pagehide', flush);
  doc.addEventListener('visibilitychange', () => { if (doc.visibilityState === 'hidden') flush(); });
};
