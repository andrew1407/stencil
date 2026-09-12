// Import one scanned row into the editor tab this panel stands on. Resolves false when
// nothing landed, which lets the assistant fall back to a new-tab hand-off.
import { MSG } from '../lib/messages.js';
import { editableSrc, sourceOf } from '../lib/imageModel.js';
import { promptImportMode } from './editorDialogs.js';

export const createImportHere = ({ ask, getEditorTabId, setStatus, imageDataUrl,
                                   refreshEditors, dismiss }) => {
  // `src` is the bytes to load, `source` the provenance the hand-off records. `src` is
  // resolved here because an SVG must be rasterised first and only a document can draw one.
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
    resource: image.resource || '',
    incognito: !!incognito,
    ...(crop ? { crop } : {}),
    ...(page ? { page } : {}),
    mode,
  });

  // `mode:'ask'`: a blank editor imports at once, an occupied one returns `needsChoice`.
  const importHere = async (image, { incognito = false, crop = null, page = '', anchor = null } = {}) => {
    if (getEditorTabId() == null) return false;
    // A server row's bytes sit behind Bearer auth the worker's fetch cannot present.
    if (image.shared) {
      setStatus('A server-stored image opens in its own editor tab — use ⋯ → Open ▸ In editor.');
      return false;
    }
    if (!editableSrc(image)) {
      setStatus(`“${image.name}” has no image to open — a video needs a captured frame first.`);
      return false;
    }
    setStatus('Loading image…');
    // Resolved once: the chooser round-trip must not re-fetch and re-rasterise.
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
