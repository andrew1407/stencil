// The Servers modal's markup: the connect form, the auto-connect/sync checks, the credential
// filter and the batch bar. Split out of ui/connectModal.js, which wires the list.
import { icon } from './icons.js';

export const connectModalInner = () => `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('server', { size: 18 })} Servers</h2>
                <button class="app-modal-close btn-icon-text" id="connect-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Connect a server</div>
                <div class="vs-row vs-field"><label data-title="Server URL, e.g. http://localhost:8090">URL</label>
                    <input type="text" id="connect-url" placeholder="http://localhost:8090">
                </div>
                <div class="vs-row vs-field"><label data-title="Optional access token (issued otherwise)">Token</label>
                    <input type="text" id="connect-token" placeholder="(optional)">
                </div>
                <div class="vs-row vs-actions">
                    <button id="connect-add" class="btn-icon-text" data-title="Connect to the server at the URL above">${icon('plus-circle', { size: 14 })}<span>Connect</span></button>
                    <button id="connect-reconnect" class="btn-icon-text" data-title="Re-establish every connection">${icon('refresh', { size: 15 })}<span>Reconnect all</span></button>
                </div>
                <div class="vs-row vs-checks">
                    <label class="vs-inline-check" data-title="Reconnect saved servers automatically when the editor opens">
                        <input type="checkbox" id="connect-autoconnect"> Auto-connect on open
                    </label>
                    <label class="vs-inline-check" data-title="When off, edits to a fetched server project stay in this session only — never pushed to the server or saved locally (download or 'Make local copy' to keep them)">
                        <input type="checkbox" id="connect-sync"> Sync changes to server
                    </label>
                </div>

                <!-- Heading + credential filter share one row; the filter is view state
                     only, never persisted. -->
                <div class="connect-section-row">
                    <div class="vs-section">Connections</div>
                    <select id="connect-filter" class="modal-filter" data-title="Filter connections by credential">
                        <option value="all">All</option>
                        <option value="admin">Admin</option>
                        <option value="non-admin">Non-admin</option>
                    </select>
                </div>
                <!-- Batch-select toolbar: the projects bar's shape — it stays while the list
                     has rows (it hosts Select all), and only the count + the selection
                     actions come and go with the checked set (updateBatchBar). -->
                <div class="connect-batch-bar" id="connect-batch-bar" style="display:none">
                    <span class="connect-batch-count" id="connect-batch-count" style="display:none">0 selected</span>
                    <span class="connect-batch-actions">
                        <!-- Select all ↔ Deselect all: the one toggle is also the bar's "clear"
                             (a separate Clear did the same thing as Deselect all). -->
                        <button id="connect-select-all" class="btn-icon-text" style="display:none" data-title="Select every listed connection (the current filter's rows)">${icon('check', { size: 13 })}<span>Select all</span></button>
                        <!-- The selection-only actions come and go as ONE group, so the swap is a
                             single flight instead of a button-by-button scramble. -->
                        <span class="connect-batch-selected" id="connect-batch-selected" style="display:none">
                        <button id="connect-batch-reconnect" class="btn-icon-text" data-title="Reconnect the selected servers">${icon('refresh', { size: 13 })}<span>Reconnect</span></button>
                        <button id="connect-batch-disconnect" class="danger btn-icon-text" data-title="Disconnect (and forget) the selected servers">${icon('trash', { size: 13 })}<span>Disconnect</span></button>
                        </span>
                    </span>
                </div>
                <div id="connect-list"><!-- filled by JS --></div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">Connections are saved and (optionally) restored on open · server projects show a golden outline.</span>
            </div>
        </div>
    `;
