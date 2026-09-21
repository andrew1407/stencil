// What the Open-Image dialog's three buttons do: resolve the chosen source to a still-image
// File, then open it here, in a new tab, or over the current project. Split out of openImageModal.
import { notify } from '../../utils.js';
import { fetchUrlToFile, toFrameIfVideo } from '../../core/image/imageSourceLoader.js';

export function wireOpenActions({ app, els, preview, src, frameSeconds, openOpts, target, canReplace, close }) {
  const { hereBtn, newTabBtn, replaceBtn, incog, renameEl, keepEl } = els;

  // The active tab's source as a still-image File, or null on error (already notified).
  const resolveSource = async () => {
    try {
      const file = preview.tab() === 'url' ? await fetchUrlToFile(src.urlVal()) : src.chosenFile();
      if (!file) return null;
      return await toFrameIfVideo(file, frameSeconds());
    } catch (e) {
      notify(preview.tab() === 'url'
        ? `Could not load that URL — ${e.message}. Cross-origin URLs need CORS headers; try the extension or desktop app.`
        : `Could not capture a video frame — ${e.message}`, 'fail');
      return null;
    }
  };

  hereBtn.addEventListener('click', async () => {
    if (!src.hasSource()) return;
    const address = target() || null;
    const opts = openOpts();
    const resolved = await resolveSource();
    if (!resolved) return;
    app.openImageHere(resolved, incog.checked, address, opts);
    close();
  });
  newTabBtn.addEventListener('click', async () => {
    if (!src.hasSource()) return;
    const opts = openOpts();
    const resolved = await resolveSource();
    if (!resolved) return;
    app.openImageNewTab(resolved, incog.checked, opts);
    close();
  });
  replaceBtn.addEventListener('click', async () => {
    if (preview.tab() !== 'file' || !src.chosenFile() || !canReplace()) return;
    const resolved = await resolveSource();
    if (!resolved) return;
    app.replaceProjectImage(resolved, { rename: renameEl.checked, keepAnnotations: keepEl.checked });
    close();
  });
}
