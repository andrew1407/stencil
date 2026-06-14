import { StencilElement, hostTag, define, wireModalShell, attachSearchFilter, rowMatches } from './base.js';
import { notify, comboFromEvent, formatCombo } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
// ── Component: settings modal (hotkey editor) ───────────────────
export class StencilSettingsModal extends StencilElement {
  static inner() {
    return `
        <div id="settingsModal">
            <div class="settings-header">
                <h2>⚙ Settings — Keyboard Shortcuts</h2>
                <button id="settingsClose">✕ Close</button>
            </div>
            <div class="modal-search-bar">
                <input type="text" id="hotkeySearch" class="modal-search" placeholder="Search shortcuts…">
            </div>
            <div class="settings-body">
                <table class="hotkey-table" id="hotkeyTable">
                    <thead>
                        <tr><th>Action</th><th>Shortcut</th><th>Default</th><th style="width:30px;"></th></tr>
                    </thead>
                    <tbody></tbody>
                </table>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Double-click a shortcut to set a new combination · Esc cancels · ↺ resets one</span>
                <button id="resetAllHotkeys">↺ Reset All</button>
            </div>
        </div>
    `;
  }
  static template() { return hostTag('stencil-settings-modal', 'id="settingsModalOverlay"', StencilSettingsModal.inner()); }

  wire(_app) {
    const overlay = document.getElementById('settingsModalOverlay');
    const openBtn = document.getElementById('settingsBtn');
    const closeBtn = document.getElementById('settingsClose');
    const resetAll = document.getElementById('resetAllHotkeys');
    const search = document.getElementById('hotkeySearch');
    const tbody = document.querySelector('#hotkeyTable tbody');

    // {id, cell} of the row currently waiting for a key combination
    let capturing = null;

    // Hide rows that don't match the search query (matches action/shortcut/default).
    const applyFilter = () => {
      let visible = 0;
      tbody.querySelectorAll('tr').forEach(tr => {
        if (tr.classList.contains('hotkey-no-match')) return;
        const match = rowMatches(tr.textContent, search.value || '');
        tr.style.display = match ? '' : 'none';
        if (match) visible++;
      });
      let empty = tbody.querySelector('.hotkey-no-match');
      if (visible === 0) {
        if (!empty) {
          empty = document.createElement('tr');
          empty.className = 'hotkey-no-match';
          empty.innerHTML = `<td colspan="4" class="info-empty">No matching shortcuts.</td>`;
          tbody.appendChild(empty);
        }
        empty.style.display = '';
      } else if (empty) {
        empty.style.display = 'none';
      }
    };

    const rebuild = () => {
      tbody.innerHTML = '';
      HOTKEY_DEFS.forEach(def => {
        const tr = document.createElement('tr');
        // Defaults are platformized in the registry; compare/display against that.
        const def0 = hotkeys.getDefault(def.id);
        const curRaw = hotkeys.get(def.id);
        const cur = curRaw ? formatCombo(curRaw, hotkeys.isMac) : '(unset)';
        const isDefault = curRaw === def0;
        tr.innerHTML = `
                <td>${def.label}</td>
                <td><span class="hotkey-cell" data-id="${def.id}" title="Double-click to set a new combination">${cur}</span></td>
                <td><span class="hotkey-default">${formatCombo(def0, hotkeys.isMac)}</span></td>
                <td style="text-align:center;">
                    <button class="hotkey-reset-btn" data-id="${def.id}" title="Reset to default"${isDefault ? ' style="visibility:hidden;"' : ''}>↺</button>
                </td>
            `;
        tbody.appendChild(tr);
      });
      tbody.querySelectorAll('.hotkey-cell').forEach(cell => {
        cell.addEventListener('dblclick', () => startCapture(cell));
      });
      tbody.querySelectorAll('.hotkey-reset-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          hotkeys.reset(btn.dataset.id);
          hotkeys.save();
          hotkeys.updateCtxHints();
          rebuild();
        });
      });
      applyFilter(); // keep the active search applied after every rebuild
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
      if (commit && combo) {
        hotkeys.set(c.id, combo);
        hotkeys.save();
        hotkeys.updateCtxHints();
      }
      rebuild();
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
      // Conflict check
      for (const [otherId, otherCombo] of hotkeys.entries()) {
        if (otherId === capturing.id) continue;
        if (otherCombo === combo) {
          const other = HOTKEY_DEFS.find(d => d.id === otherId);
          if (!confirm(`"${formatCombo(combo, hotkeys.isMac)}" is already used by "${other.label}".\n\nUnbind it and assign this combination?`)) {
            stopCapture(false);
            return;
          }
          hotkeys.set(otherId, '');
          break;
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
    resetAll.addEventListener('click', () => {
      if (!confirm('Reset ALL keyboard shortcuts to their defaults?')) return;
      hotkeys.resetAll();
      hotkeys.save();
      hotkeys.updateCtxHints();
      rebuild();
      notify('Hotkeys reset to defaults', 'ok');
    });

    // Reflect current bindings in context-menu hints on startup
    hotkeys.updateCtxHints();
  }
}
define('stencil-settings-modal', StencilSettingsModal);
