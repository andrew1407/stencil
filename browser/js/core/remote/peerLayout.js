// A peer's edit on the picture this editor already holds: when the server's original is the one on
// screen, its layout — crop and turn included — is adopted in place as one undo step. The original
// is not re-downloaded and the undo history before it survives.
import { validateLayout, normalizeCropRect, sanitizeLines } from '../layout.js';
import { editorMemento } from '../historyStack.js';
import { CHANGE, changed } from '../app/changes.js';

// What identifies the server's picture: the SHA-256 the server took of its stored original, and a
// blank's fill. A layout push or a result upload changes neither; a record with no hash names none.
export const imageSignature = (rec) => (rec?.hasImage && rec.originalHash
  ? JSON.stringify([rec.originalHash, rec.blankColor || '']) : '');

// Both records must name the original by its hash: without one the picture is unknown and reloads.
export const samePicture = (knownSig, rec) => !!knownSig && imageSignature(rec) === knownSig;

// The SHA-256 the server records for an original's bytes (lowercase hex); '' where WebCrypto is
// missing (an insecure origin), which names no picture, so a peer's edit reloads.
export const originalHashOf = async (bytes) => {
  try {
    const sum = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
    return Array.from(sum, (b) => b.toString(16).padStart(2, '0')).join('');
  } catch { return ''; }
};

// The signature once this editor's own upload is the original: a file write leaves the blank fill.
export const withOriginal = (sig, hash) => imageSignature({
  hasImage: true, originalHash: hash, blankColor: sig ? JSON.parse(sig)[1] : '',
});

// The layout's crop, quarter turn and mirror over the original on screen, snapped as a load snaps them; null
// for a layout without a crop rect, which reloads.
const peerView = (app, layout) => {
  const o = app.originalImage;
  if (!o || !normalizeCropRect(layout?.cropRect)) return null;
  const q = Number.isInteger(layout.rotationQuarters) ? layout.rotationQuarters : 0;
  const [iw, ih] = q % 2 ? [o.height, o.width] : [o.width, o.height];
  return { cropRect: app.imageModel.roundRect(layout.cropRect, iw, ih), rotationQuarters: q, mirrored: layout.mirrored === true };
};

// Lines land only when they differ, and a crop or turn is re-derived from the original as undo does.
// One step only when lines, view or filter moved: a peer's result upload leaves no empty undo step.
export const applyPeerLayout = (app, layout, remote) => {
  const view = peerView(app, layout);
  const verdict = validateLayout(layout, {
    hasImage: true, imgW: app.canvas.width, imgH: app.canvas.height, hasExistingLines: false,
  });
  if (!view || !verdict.ok) return false;
  const filterWas = [app.imageFilter, app.filterColor];
  remote.adoptServerPageFormat(layout);
  remote.adoptServerFilter(layout);
  remote.adoptServerFormulas(layout);
  const linesMoved = JSON.stringify(verdict.lines) !== JSON.stringify(sanitizeLines(app.lines));
  if (linesMoved) {
    app.strokeFx?.cancel?.();
    app.lines = verdict.lines;
    app.hoverPt = null;
    app.hoverLineIdx = -1;
    app.listHoverLineIdx = -1;
    const gone = (i) => i >= app.lines.length;
    if (gone(app.selectedLineIdx) || (app.selectedLines || []).some(gone)) app.deselectLine(false);
    else if (app.selectedLineIdx >= 0) app.showSelectionPanel(app.lines[app.selectedLineIdx]);
    if (gone(app.coordLineIdx)) app.coordLineIdx = -1;
  }
  const viewMoved = app.imageModel.restoreView(view);
  if (linesMoved || viewMoved || app.imageFilter !== filterWas[0] || app.filterColor !== filterWas[1])
    app.history.push(editorMemento(app));
  if (viewMoved) app.imageModel.settleView({ sync: false });
  else {
    app.coordTable.update(app.coordLineIdx >= 0 ? app.lines[app.coordLineIdx].points : null);
    app.renderer.redraw();
    app.storage.saveSoon();
  }
  changed(app, CHANGE.history, CHANGE.lines, CHANGE.selection);
  return true;
};
