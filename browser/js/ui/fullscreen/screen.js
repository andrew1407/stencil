// The OS-level half of fullscreen: the Fullscreen API on the document. The in-app layer
// (layer.js) is the other half; 'screen' mode runs both, 'window' mode the layer alone.
// Desktop twin: EditorViewFullscreen.cpp showFullScreen()/showNormal().

const doc = () => (typeof document === 'undefined' ? null : document);

export const isScreenFull = () => !!doc()?.fullscreenElement;

// Resolves false when the page may not go fullscreen (no user activation, an iframe without
// allow="fullscreen", no API): the caller keeps the in-window layer instead.
let refusal = '';
export const enterScreen = async () => {
  const el = doc()?.documentElement;
  refusal = '';
  if (!el?.requestFullscreen) { refusal = 'no Fullscreen API'; return false; }
  if (isScreenFull()) return true;
  try { await el.requestFullscreen({ navigationUI: 'hide' }); return true; } catch (e) {
    refusal = String(e?.message || e || 'refused');
    return false;
  }
};
// Why the last enterScreen came back false ('' when it did not): the browser's own words.
export const screenRefusal = () => refusal;

export const exitScreen = async () => {
  if (!isScreenFull() || !doc().exitFullscreen) return;
  try { await doc().exitFullscreen(); } catch { /* already left */ }
};

// Returns the unsubscribe.
export const onScreenChange = (cb) => {
  const d = doc();
  if (!d?.addEventListener) return () => {};
  const fn = () => cb(isScreenFull());
  d.addEventListener('fullscreenchange', fn);
  return () => d.removeEventListener('fullscreenchange', fn);
};
