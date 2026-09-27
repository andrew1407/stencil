// Paste + drop on the panel: image/video files become attachments (mediaFilesFromData,
// the global wiring's extraction); text pastes stay native.
import { notify } from '../../../utils.js';
import { mediaFilesFromData, extractDraggedImageUrl, extractDraggedImageUrls, fetchFirstDraggedMediaFile } from '../../../core/pointer/dragImageUrl.js';

export function wirePanelDrop({ host, dropRow, attachFiles }) {
  host.addEventListener('paste', async (e) => {
    if (!host.classList.contains('chat-open')) return;
    const files = mediaFilesFromData(e.clipboardData);
    if (!files.length) return;
    e.preventDefault();
    e.stopPropagation();
    await attachFiles(files);
  });
// The composer acts on a drop, but the whole panel swallows one: falling through popped
// the canvas's "Open dropped image" dialog. A miss says where to aim.
  host.addEventListener('dragover', (e) => {
    if (!host.classList.contains('chat-open')) return;
    e.preventDefault();
    // …and it must still bubble: the document's drop-owner branch hides the global overlay.
    try { e.dataTransfer.dropEffect = 'copy'; } catch { /* older DnD */ }
  });
  host.addEventListener('drop', (e) => {
    if (!host.classList.contains('chat-open')) return;
// The composer's own handler runs first and stops propagation.
    e.preventDefault();
    e.stopPropagation();
    if (mediaFilesFromData(e.dataTransfer).length
        || extractDraggedImageUrl((t) => e.dataTransfer.getData(t))) {
      notify('Drop it on the message box to attach it', 'info');
    }
  });
// The text box is the drop target, not the whole row (desktop showDropCue parity).
  dropRow.addEventListener('dragover', (e) => {
    if (!host.classList.contains('chat-open')) return;
    e.preventDefault();
// Must bubble to the document dragover: [data-drop-owner] tells it this row handles its own drops.
    try { e.dataTransfer.dropEffect = 'copy'; } catch { /* older DnD */ }
    dropRow.classList.add('chat-drop-target');
  });
  dropRow.addEventListener('dragleave', (e) => {
    if (!dropRow.contains(e.relatedTarget)) dropRow.classList.remove('chat-drop-target');
  });
  dropRow.addEventListener('drop', async (e) => {
    dropRow.classList.remove('chat-drop-target');
    if (!host.classList.contains('chat-open')) return;
    e.preventDefault();
    e.stopPropagation();
    const files = mediaFilesFromData(e.dataTransfer);
    if (files.length) { await attachFiles(files); return; }
// An image dragged from another page carries no File, just a URL in uri-list/html.
    const urls = extractDraggedImageUrls((t) => e.dataTransfer.getData(t));
    if (!urls.length) { notify('Nothing to attach from that drop', 'fail'); return; }
    try {
      await attachFiles([await fetchFirstDraggedMediaFile(urls, { accept: /^(image|video)\// })]);
    } catch (err) {
      notify(`Couldn't attach that image — ${err.message}. `
        + 'If the site blocks cross-origin downloads, try the extension or desktop app.', 'fail');
    }
  });
}
