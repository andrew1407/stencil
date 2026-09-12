import { connectionByUrl, createProject, projectRequestFromImage } from '../lib/connections.js';
import { sourceOf, hostLabel } from '../lib/imageModel.js';
import { enhanceSelect } from '../lib/customSelect.js';
import { openPanelDialog } from './dialogShell.js';
import { statusEl } from './panelDom.js';
import { state } from './model.js';
import { applyFilters } from './filters.js';
import { loadShared } from './sharedPins.js';
import { setPinnedState } from './pinActions.js';

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

export const pinWithPrompt = async (image, anchor) => {
  const target = await promptPinTarget(anchor);   // undefined = cancel, '' = local, url = server
  if (target === undefined) return;
  if (!image.pinned) await setPinnedState(image, true);
  if (target) await storeOnServer(image, target);
};

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
    // Built after the page's own enhanceSelect pass, so it asks for its custom list itself.
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
