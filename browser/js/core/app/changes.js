// The editor's change feed: an edit names the inputs it moved, never the controls to repaint.
// ui/control/state.js subscribes each control area to the channels it reads, and FLUSH runs every
// area one signal dirtied, once. `app.changes` is the Emitter (core/emitter.js) the app is wired with.
export const CHANGE = Object.freeze({
  history: 'history',       // the undo cursor, or the in-progress stroke's points
  lines: 'lines',           // the committed line set
  selection: 'selection',
  drawing: 'drawing',       // draw mode on or off
  compare: 'compare',       // the read-only comparison view
  project: 'project',       // the active project, incognito, its server or file link
});
export const FLUSH = 'flush';

// No feed (a partial app under test) is a no-op.
export const changed = (app, ...topics) => {
  const feed = app.changes;
  if (!feed) return;
  for (const topic of topics) feed.emit(topic);
  feed.emit(FLUSH);
};
