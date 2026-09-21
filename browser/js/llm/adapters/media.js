// ── §10 media adapters: the picture, its frames, and the clipboard ──────────
// The slice of the chat controller's bag that reads or writes the working image.
// Every one is a thin call onto the same app methods the toolbar uses.
import { downscaleImageToDataUrl } from '../chat/chatController.js';
import { videoFrameSamples, videoFrameByIndex } from '../../core/videoFrame.js';
export const mediaAdapters = (app) => ({
  // Guarded: a plan wanting variants / ask previews on an EMPTY editor must
  // fail with words, not a raw canvas drawImage(null) TypeError.
  exportImage: async () => {
    if (!app.image) throw new Error('No image is loaded in the editor — load or create one first');
    return app.export.renderExportCanvas().toDataURL('image/png');
  },
  prepareAttachment: (file) => downscaleImageToDataUrl(file),
  extractFrames: (file, n) => videoFrameSamples(file, n),
  frameAt: (file, i) => videoFrameByIndex(file, i),
  // §10 openUrl with incognito: adopt incognito IN PLACE (desktop's openSourceHere), NOT a
  // new tab — the rest of the plan and the §7 continuation run against this editor's picture.
  openIncognito: async (url) => { await window.stencil.load(url, { incognito: true }); },
  // §10 copy: the clipboard write's REAL outcome, so a blocked write becomes a
  // reply warning instead of only a transient toast.
  copyRendered: () => app.export.copyImageToClipboard(),
  // §10 copy what:"layout": same outcome-promise pattern for the layout JSON.
  copyLayoutRendered: () => app.export.copyLayoutToClipboard(),
});
