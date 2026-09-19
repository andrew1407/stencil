import { notify, isTypingTarget, pointInRect } from '../../utils.js';
import { extractDraggedImageUrl, mediaFilesFromData, fetchDraggedMediaFile } from '../../core/dragImageUrl.js';
import { showDropOverlay, hideDropOverlay } from '../dropOverlay.js';
import { loadScriptFile } from '../scriptModal.js';
export function wireDropPaste(app) {
  // Document-wide drag-and-drop overlay, split into LEFT (upload + save) and RIGHT
  // (upload incognito) zones. The cursor's half of the window decides which.
  const dropZone = document.getElementById('global-drop-overlay');
  const dropLeftHalf = (e) => e.clientX < window.innerWidth / 2;
  const clearZoneCue = () => dropZone.querySelectorAll('.drop-zone-active').forEach((z) => z.classList.remove('drop-zone-active'));

  // An image dragged from ANOTHER web page arrives as a URL, not a File (see dragImageUrl.js).
  const draggedImageUrl = (dt) => extractDraggedImageUrl((t) => dt.getData(t));
  // Fetch a dragged image URL into a File so it flows through the same load path as a
  // dropped file (shared with the chat's drop-to-attach — dragImageUrl.js).
  const fetchUrlToFile = (url) => fetchDraggedMediaFile(url);

  // `from` = the drop point in client coords — the canvas plays in out of it (ui/motion.js
  // arriveFrom); the dialog and paste paths have none.
  const handleImageDrop = async (file, incognito, from = null) => {
    if (app.image) {
      // Three BUTTONS, not a picker: two answers and a way out, each one click. Desktop
      // parity — MainWindow.cpp asks the same question with This window / New window / Cancel.
      const where = await app.askAlt('An image is already open. Where should the dropped image open?', {
        title: 'Open dropped image',
        confirmLabel: 'Open in the current page', confirmIcon: 'image',
        altLabel: 'Open in a new page', altIcon: 'external',
      });
      if (!where) return;                                                    // Cancel / Escape
      if (where === 'alt') { app.openImageNewTab(file, incognito); return; }
    }
    // `landing` = play the canvas reveal — this image arrived by drop, not by dialog.
    app.openImageHere(file, incognito, null, { landing: true, from });
  };

  // Internal row-reorder drags (Servers / Projects modals) carry this flag; the image-drop
  // overlay must ignore them, or a connection row's URL is fetched as an image.
  const isReorderDrag = (e) => { try { return e.dataTransfer.types.includes('application/x-stencil-reorder'); } catch { return false; } };

  // Drops on an element wiring its OWN drop handlers belong to it, so the overlay stands down
  // over its rect. Owners declare themselves with [data-drop-owner] while visible.
  const overDropOwner = (x, y) => {
    for (const el of document.querySelectorAll('[data-drop-owner]')) {
      if (pointInRect(x, y, el.getBoundingClientRect())) return true;
    }
    return false;
  };

  document.addEventListener('dragenter', e => {
    e.preventDefault();
    if (isReorderDrag(e)) return;   // internal row reorder — not an image drop
    if (overDropOwner(e.clientX, e.clientY)) { hideDropOverlay(dropZone); clearZoneCue(); return; }
    // Show the overlay for a dragged File OR an image dragged from another page (uri-list /
    // html — a URL, not a File). Plain text alone isn't treated as a drop (too noisy).
    const t = e.dataTransfer.types;
    if (t.includes('Files') || t.includes('text/uri-list') || t.includes('text/html')) showDropOverlay(dropZone);
  });
  document.addEventListener('dragover', e => {
    e.preventDefault();
    if (isReorderDrag(e)) return;   // internal row reorder — leave it to the modal's own handlers
    // Over a drop owner (the open chat panel): hide the overlay so it gets the drop.
    if (overDropOwner(e.clientX, e.clientY)) { hideDropOverlay(dropZone); clearZoneCue(); return; }
    e.dataTransfer.dropEffect = 'copy';
    // Highlight the half the cursor is over so the save/incognito choice is legible.
    if (dropZone.style.display !== 'none' && dropZone.style.display !== '') {
      const left = dropLeftHalf(e);
      const lz = dropZone.querySelector('.drop-zone-left');
      const rz = dropZone.querySelector('.drop-zone-right');
      if (lz) lz.classList.toggle('drop-zone-active', left);
      if (rz) rz.classList.toggle('drop-zone-active', !left);
    }
  });
  document.addEventListener('dragleave', e => {
    // Only hide when leaving the entire window
    if (e.relatedTarget === null) { hideDropOverlay(dropZone); clearZoneCue(); }
  });
  // A drag that ends without a drop landing here (cancelled, or claimed by an
  // overlay that swallowed the events) must never strand the zones on screen.
  document.addEventListener('dragend', () => { hideDropOverlay(dropZone); clearZoneCue(); });
  document.addEventListener('drop', e => {
    e.preventDefault();
    hideDropOverlay(dropZone);
    clearZoneCue();
    if (isReorderDrag(e)) return;   // internal row reorder — don't try to load an image
    if (overDropOwner(e.clientX, e.clientY)) return;   // the owner's (chat panel's) drop handler owns it
    const incognito = !dropLeftHalf(e);   // RIGHT half = incognito, LEFT half = upload + save
    const from = { x: e.clientX, y: e.clientY };
    const file = e.dataTransfer.files[0];
    if (!file) {
      // No File → maybe an image dragged from another page (a URL). Fetch it into a File.
      const url = draggedImageUrl(e.dataTransfer);
      if (!url) {
        // A relative <img src> carries no origin, so there is nothing to fetch —
        // say so instead of silently doing nothing (dragImageUrl.js absolutize).
        notify('Could not read an image URL from that drag — try dragging the image from its own page, or copy the image address and use Open image → URL', 'fail');
        return;
      }
      // Surface the real failure (bad URL, 404, non-image response) instead of
      // blaming CORS unconditionally.
      fetchUrlToFile(url)
        .then((f) => handleImageDrop(f, incognito, from))
        .catch((err) => notify(`Could not load the dragged image — ${err.message}. `
          + 'If the site blocks cross-origin downloads, try the extension or desktop app.', 'fail'));
      return;
    }
    if (file.name.endsWith('.stencil')) {
      app.export.openProjectFile(file, { from });   // a whole .stencil project ignores the save/incognito split
    } else if (file.name.endsWith('.stc')) {
      loadScriptFile(file);   // into the script window when it is open, else it runs
    } else if (file.type.startsWith('image/')) {
      handleImageDrop(file, incognito, from);
    } else if (file.name.endsWith('.json') || file.type === 'application/json') {
      app.loadJSONFromFile(file, { from });   // a .json layout ignores the save/incognito split
    } else {
      notify('Please drop an image, a .json layout, a .stencil project, or a .stc script', 'fail');
    }
  });

  // Clipboard paste (Ctrl+V) — handles images and JSON layout text
  document.addEventListener('paste', async e => {
    if (isTypingTarget(e.target)) return; // let native paste work in inputs
    const cd = e.clipboardData;
    if (!cd) return;

    // mediaFilesFromData reads the items SYNCHRONOUSLY — clipboardData is invalid after an await.
    // The chat panel shares it for attach-on-paste and stops propagation before this handler.
    const hasImageItem = [...cd.items].some((item) => item.type && item.type.startsWith('image/'));
    if (hasImageItem) {
      e.preventDefault();
      const file = mediaFilesFromData(cd).find((f) => f.type.startsWith('image/'));
      if (app.image && !(await app.confirm('Replace current image with pasted image?', { title: 'Replace image', confirmIcon: 'paste' }))) {
        notify('Image paste canceled', 'info');   // a declined confirm is a notice, not a failure
        return;
      }
      if (file) {
        app.loadImageFromFile(file);
        notify('Image pasted from clipboard', 'ok');
      } else {
        notify('Could not read pasted image', 'fail');
      }
      return;
    }

    // 2) Text — try to parse as layout JSON
    const text = cd.getData('text/plain');
    if (text) {
      let data = null;
      try {
        data = JSON.parse(text);
      } catch {
        /* not layout JSON — left as null; the guard below simply ignores the paste */
      }
      if (data && Array.isArray(data.lines)) {
        e.preventDefault();
        app.export.applyPastedLayout(data);
      }
    }
  });
}
