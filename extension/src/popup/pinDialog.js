import { connectionByUrl, createProject, projectRequestFromImage } from '../lib/connections.js';
import { sourceOf, hostLabel } from '../lib/imageModel.js';
import { enhanceSelect } from '../lib/customSelect.js';
import { openPanelDialog } from './dialogShell.js';
import { statusEl } from './panelDom.js';
import { state } from './model.js';
import { applyFilters } from './filters.js';
import { loadShared } from './sharedPins.js';
import { setPinnedState } from './pinActions.js';

// Store an already-pinned image on a server as a shared project.
const storeOnServer = async (image, serverUrl) => {
  const conn = serverUrl ? connectionByUrl(state.connections, serverUrl) : null;
  if (!conn) return;
  try {
    statusEl.textContent = `Saving to ${hostLabel(conn.url)}…`;
    await createProject(conn, projectRequestFromImage({ name: image.name, source: sourceOf(image) }, state.activeUrl));
    statusEl.textContent = `Saved to ${hostLabel(conn.url)}.`;
    await loadShared();
    applyFilters();
  } catch (err) {
    statusEl.textContent = `Server save failed: ${err.message}`;
  }
};

// Pin an image, asking WHERE via the target-selector dialog (Cancel aborts entirely).
// `anchor` is the control that asked — the row's pin button or the ⋯ menu's button —
// so the dialog opens next to it rather than covering the panel.
export const pinWithPrompt = async (image, anchor) => {
  const target = await promptPinTarget(anchor);   // undefined = cancel, '' = local, url = server
  if (target === undefined) return;            // cancelled — don't pin
  if (!image.pinned) await setPinnedState(image, true);
  if (target) await storeOnServer(image, target);
};

// In-popup dialog asking WHERE to pin (Pin locally / Store on each connected server).
// Resolves the chosen server URL, '' for local, or undefined when cancelled. With an
// `anchor` it opens as a popover next to that control instead of the centred dialog.
const promptPinTarget = (anchor) => openPanelDialog({
  anchor,
  build: (finish) => {
    const title = document.createElement('div');
    title.className = 'dialog-title';
    title.textContent = 'Where do you want to pin this image?';

    const sel = document.createElement('select');
    sel.className = 'dialog-select';
    sel.innerHTML = '<option value="">Pin locally only</option>'
      + state.connections.map((c) => `<option value="${c.url}">Pin & store on ${hostLabel(c.url)}</option>`).join('');
    // Built after the page's own pass, so it asks for its custom list itself — otherwise
    // this one dialog would still open the OS's centred grey popup over the panel.
    queueMicrotask(() => enhanceSelect(sel));

    const row = document.createElement('div');
    row.className = 'dialog-actions';
    const cancel = document.createElement('button');
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', () => finish(undefined));
    const ok = document.createElement('button');
    ok.className = 'primary';
    ok.textContent = 'Pin';
    ok.addEventListener('click', () => finish(sel.value));
    row.append(cancel, ok);
    return [title, sel, row];
  },
});
