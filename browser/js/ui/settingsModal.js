import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { notify, comboFromEvent, formatCombo } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { icon } from './icons.js';
import { keysHtml } from './tipContent.js';
import { markIn } from './motion.js';
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
// ── Component: settings modal (hotkey editor) ───────────────────
export class StencilSettingsModal extends StencilElement {
  static inner() {
    return `
        <div id="settings-modal">
            <div class="settings-header">
                <h2>${icon('gear', { size: 18 })} Keyboard Shortcuts</h2>
                <button id="settings-close" class="btn-icon-text">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="hotkey-search" class="modal-search" placeholder="Search shortcuts…">
            </div>
            <div class="settings-body">
                <div class="hotkey-table" id="hotkey-table" role="table">
                    <div class="hotkey-head" role="row">
                        <span role="columnheader">Action</span><span role="columnheader">Current shortcut</span><span role="columnheader">Default</span><span role="columnheader"></span>
                    </div>
                    <div class="hotkey-rows" role="rowgroup"></div>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Double-click a shortcut to set a new combination · Esc cancels · reset icon clears one</span>
                <button id="reset-all-hotkeys" class="btn-icon-text">${icon('rotate-ccw', { size: 14 })}<span>Reset All</span></button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-settings-modal', 'id="settings-modal-overlay"', StencilSettingsModal.inner()); }

  wire(app) {
    const overlay = document.getElementById('settings-modal-overlay');
    const openBtn = document.getElementById('settings-btn');
    const closeBtn = document.getElementById('settings-close');
    const resetAll = document.getElementById('reset-all-hotkeys');
    const search = document.getElementById('hotkey-search');
    const rows = document.querySelector('#hotkey-table .hotkey-rows');

    // {id, cell} of the row currently waiting for a key combination
    let capturing = null;

    // The combo as the keycaps every tooltip wears, or a muted "(unset)".
    const capsOf = combo => combo
      ? keysHtml(formatCombo(combo, hotkeys.isMac), hotkeys.isMac)
      : '<span class="hotkey-unset">(unset)</span>';

    // Hide rows that don't match the search query (matches action/shortcut/default).
    const applyFilter = () => {
      let visible = 0;
      rows.querySelectorAll('.hotkey-row').forEach(tr => {
        const match = rowMatches(tr.textContent, search.value || '');
        tr.style.display = match ? '' : 'none';
        if (match) visible++;
      });
      let empty = rows.querySelector('.hotkey-no-match');
      if (visible === 0) {
        if (!empty) {
          empty = document.createElement('div');
          empty.className = 'hotkey-no-match info-empty';
          empty.textContent = 'No matching shortcuts.';
          rows.appendChild(empty);
        }
        empty.style.display = '';
      } else if (empty) {
        empty.style.display = 'none';
      }
    };

    // `formed` — ids whose combo just changed: their new keycaps arrive as dust (markIn).
    const rebuild = (formed = []) => {
      rows.innerHTML = '';
      HOTKEY_DEFS.forEach(def => {
        const tr = document.createElement('div');
        // .shimmer: the app-wide glass sweep on hover (css/layout.css), like a list row.
        tr.className = 'hotkey-row shimmer';
        tr.setAttribute('role', 'row');
        tr.dataset.id = def.id;
        // Defaults are platformized in the registry; compare/display against that.
        const def0 = hotkeys.getDefault(def.id);
        const curRaw = hotkeys.get(def.id);
        const isDefault = curRaw === def0;
        tr.innerHTML = `
                <span class="hotkey-td hotkey-action" role="cell">${def.label}</span>
                <span class="hotkey-td" role="cell"><span class="hotkey-cell" data-id="${def.id}" title="Double-click to set a new combination">${capsOf(curRaw)}</span></span>
                <span class="hotkey-td" role="cell"><span class="hotkey-default">${capsOf(def0)}</span></span>
                <span class="hotkey-td hotkey-td-reset" role="cell">
                    <button class="hotkey-reset-btn" data-id="${def.id}" title="Reset to default"${isDefault ? ' style="visibility:hidden;"' : ''}>${icon('rotate-ccw', { size: 15 })}</button>
                </span>
            `;
        rows.appendChild(tr);
      });
      rows.querySelectorAll('.hotkey-cell').forEach(cell => {
        cell.addEventListener('dblclick', () => startCapture(cell));
      });
      rows.querySelectorAll('.hotkey-reset-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          const before = hotkeys.get(btn.dataset.id);
          hotkeys.reset(btn.dataset.id);
          hotkeys.save();
          hotkeys.updateCtxHints();
          rebuild(before === hotkeys.get(btn.dataset.id) ? [] : [btn.dataset.id]);
        });
      });
      applyFilter(); // keep the active search applied after every rebuild
      // Past the mark budget the rest simply appear: one gesture, not thirty clouds.
      formed.slice(0, 8).forEach(id => {
        const cell = rows.querySelector(`.hotkey-cell[data-id="${id}"]`);
        if (cell) markIn(cell);
      });
    };

    const startCapture = cell => {
      if (capturing) stopCapture(false);
      capturing = { id: cell.dataset.id, cell };
      cell.classList.add('capturing');
      cell.textContent = 'press combination…';
    };
    const stopCapture = (commit, combo) => {
      if (!capturing) return;
      const c = capturing;
      capturing = null;
      const changed = commit && combo && combo !== hotkeys.get(c.id);
      if (changed) {
        hotkeys.set(c.id, combo);
        hotkeys.save();
        hotkeys.updateCtxHints();
      }
      rebuild(changed ? [c.id] : []);
    };

    // Capture-phase keydown so the global hotkey dispatcher doesn't fire while editing
    document.addEventListener('keydown', e => {
      if (!capturing) return;
      if (e.key === 'Escape') {
        e.preventDefault(); e.stopPropagation();
        stopCapture(false);
        return;
      }
      const combo = comboFromEvent(e);
      if (!combo) return; // pure modifier — keep waiting
      e.preventDefault(); e.stopPropagation();
      // A combo another action owns is refused — the other action is never silently unbound.
      for (const [otherId, otherCombo] of hotkeys.entries()) {
        if (otherId === capturing.id) continue;
        if (otherCombo === combo) {
          const other = HOTKEY_DEFS.find(d => d.id === otherId);
          notify(`"${formatCombo(combo, hotkeys.isMac)}" is already used by "${other?.label || otherId}" — the old shortcut was kept`, 'error');
          stopCapture(false);
          return;
        }
      }
      stopCapture(true, combo);
    }, true);

    attachSearchFilter(search, applyFilter);
    wireModalShell(overlay, openBtn, closeBtn, {
      onOpen: () => { search.value = ''; rebuild(); },
      onClose: () => { if (capturing) stopCapture(false); },
      escapeClose: false
    });
    resetAll.addEventListener('click', async () => {
      if (!(await app.confirm('Reset ALL keyboard shortcuts to their defaults?', { title: 'Reset shortcuts', danger: true, confirmIcon: 'refresh' }))) return;
      const changed = HOTKEY_DEFS.map(d => d.id).filter(id => hotkeys.get(id) !== hotkeys.getDefault(id));
      hotkeys.resetAll();
      hotkeys.save();
      hotkeys.updateCtxHints();
      rebuild(changed);
      notify('Hotkeys reset to defaults', 'ok');
    });

    // Reflect current bindings in context-menu hints on startup
    hotkeys.updateCtxHints();
  }
}
define('stencil-settings-modal', StencilSettingsModal);
