import { StencilElement, hostTag, define, wireModalShell } from '../base.js';
import { connectModalInner } from './markup.js';
import { createConnectList } from './list.js';
import { wireConnectBatch } from './batch.js';
import { wireConnectForm } from './form.js';
import { getAutoConnect, setAutoConnect, getSyncToServer, setSyncToServer } from '../../net/connectionStore.js';
import { canRefreshList } from '../../core/project/openGesture.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';

export { matchesConnFilter, batchNote } from './rules.js';

// The server connections modal, backed by app.connections (connectionManager).
export class StencilConnectModal extends StencilElement {
  static inner() { return connectModalInner(); }
  static template() { return hostTag('stencil-connect-modal', 'id="connect-modal-overlay" class="app-modal-overlay"', StencilConnectModal.inner()); }

  wire(app) {
    const $ = (id) => document.getElementById(id);
    const overlay = $('connect-modal-overlay');
    const urlEl = $('connect-url');
    const tokenEl = $('connect-token');
    const addBtn = $('connect-add');
    const reconnectBtn = $('connect-reconnect');
    const list = $('connect-list');
    // Read lazily: the connection manager is created by createStencil() after stencil:ready.
    const mgr = () => app.connections;

    const view = createConnectList(app, {
      overlay, list, reconnectBtn, mgr,
      filterEl: $('connect-filter'),
      batchBar: $('connect-batch-bar'),
      batchCount: $('connect-batch-count'),
      selectAllBtn: $('connect-select-all'),
      selectedGroup: $('connect-batch-selected'),
    });
    const { selected, hold, drag, render } = view;
    const batchBtns = {
      reconnect: $('connect-batch-reconnect'),
      disconnect: $('connect-batch-disconnect'),
    };
    wireConnectBatch(app, { list, mgr, batchBtns }, view);
    wireConnectForm({ urlEl, tokenEl, addBtn, reconnectBtn, list, mgr }, view);

    const autoEl = $('connect-autoconnect');
    autoEl.checked = getAutoConnect();
    autoEl.addEventListener('change', () => setAutoConnect(autoEl.checked));
    const syncEl = $('connect-sync');
    syncEl.checked = getSyncToServer();
    syncEl.addEventListener('change', () => setSyncToServer(syncEl.checked));

    wireModalShell(overlay, $('connect-btn'), $('connect-close'), {
      // A selection is a transient of one visit, as in the projects modal.
      onOpen: () => { autoEl.checked = getAutoConnect(); syncEl.checked = getSyncToServer(); selected.clear(); render(); },
      // Closing mid-animation finalizes every pending wipe, so a half-removed row cannot reappear.
      onClose: () => hold.finalizeAll(), focusOnOpen: urlEl,
    });

    // A refused saved session must be visible without opening this modal: the Servers
    // button's tooltip says so, mirrored onto the fullscreen toolbar clone. No badge.
    const syncExpiredBadge = () => {
      const on = (mgr()?.expiredUrls?.length || 0) > 0;
      for (const el of [$('connect-btn'), ...document.querySelectorAll('#fs-controls-panel #connect-btn')]) {
        if (el) {
          el.dataset.title = on
            ? 'Servers — a saved session expired, reconnect to sign in again'
            : 'Servers — connect to share & co-edit projects';
        }
      }
    };

    syncExpiredBadge();
    subscribe(EVENTS.connectionsChanged, () => {
      syncExpiredBadge();
      // A live event must not re-render mid-drag or mid-wipe (canRefreshList).
      if (canRefreshList({
        open: overlay.classList.contains('modal-open'),
        dragging: drag.isDragging(),
        removing: hold.holding,
      })) render();
    });
  }
}
define('stencil-connect-modal', StencilConnectModal);
