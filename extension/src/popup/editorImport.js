// ── Import one scanned row INTO the editor tab this panel stands on ─────────
// Asks first when that editor already holds an image (editorDialogs.js). Resolves
// false when nothing landed — what lets the assistant fall back to a new-tab hand-off.
import { MSG } from '../lib/messages.js';
import { editableSrc, sourceOf } from '../lib/imageModel.js';
import { promptImportMode } from './editorDialogs.js';

export const createImportHere = ({ ask, getEditorTabId, setStatus, imageDataUrl,
                                   refreshEditors, dismiss }) => {
  // The descriptor names bytes and provenance separately: `src` is the image to load, `source`
  // what the hand-off records (a video row's frame vs its media URL). `src` is resolved HERE
  // via imageDataUrl — an SVG must be rasterised first and only a document can draw one.
  const importDescriptor = (image, dataUrl) => ({
    name: image.name,
    kind: 'img',
    src: dataUrl,
    source: sourceOf(image) || editableSrc(image),
  });

  const importOnce = (image, { dataUrl, incognito, crop, page, mode }) => ask({
    type: MSG.EDITOR_IMPORT,
    tabId: getEditorTabId(),
    image: importDescriptor(image, dataUrl),
    resource: image.resource || '',      // the row remembers which page it came from
    incognito: !!incognito,
    // Crop rect + page key ride along untouched; only the assistant's `open` op sets them.
    ...(crop ? { crop } : {}),
    ...(page ? { page } : {}),
    mode,
  });

  // Open one scanned row INTO the editor tab this panel stands on. Sent `mode:'ask'`: a
  // blank editor imports at once, an occupied one returns `needsChoice` for the chooser
  // above (pinned to `anchor` when given). Resolves true once the image landed; false
  // when nothing was imported — what lets the assistant fall back to the new-tab hand-off.
  const importHere = async (image, { incognito = false, crop = null, page = '', anchor = null } = {}) => {
    if (getEditorTabId() == null) return false;
    // A shared (server) row's bytes sit behind Bearer auth the worker's fetch can't present —
    // those open through the ⋯ menu's "In editor", which resolves them here instead.
    if (image.shared) {
      setStatus('A server-stored image opens in its own editor tab — use ⋯ → Open ▸ In editor.');
      return false;
    }
    if (!editableSrc(image)) {
      setStatus(`“${image.name}” has no image to open — a video needs a captured frame first.`);
      return false;
    }
    setStatus('Loading image…');
    // Resolved once, then reused for the retry below: the chooser round-trip must not
    // re-fetch (and re-rasterise) the same image.
    let dataUrl;
    try {
      dataUrl = await imageDataUrl(image);
    } catch (err) {
      setStatus(`Import failed: ${err.message}`);
      return false;
    }
    setStatus('Importing into this editor…');
    let res = await importOnce(image, { dataUrl, incognito, crop, page, mode: 'ask' });
    if (!res.ok && res.needsChoice) {
      const mode = await promptImportMode(res.state || {}, anchor);
      if (!mode) {
        setStatus('');
        return false;
      }
      res = await importOnce(image, { dataUrl, incognito, crop, page, mode });
    }
    if (!res.ok) {
      setStatus(`Import failed: ${res.error}`);
      return false;
    }
    setStatus(`Imported into “${res.projectName || 'this editor'}”.`);
    await refreshEditors();
    dismiss();
    return true;
  };

  return importHere;
};
