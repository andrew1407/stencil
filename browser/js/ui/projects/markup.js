import { icon } from '../icons.js';

// The modal's static shell. Rows are built at runtime, so #projects-list ships empty.
export function projectsModalInner() {
    return `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('layers', { size: 18 })} Projects</h2>
                <button class="app-modal-close btn-icon-text" id="projects-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <!-- Search full-width on its own row; the filter selects sit on the row below
                 (user decision — the shared single-row bar squeezed the search box). -->
            <div class="modal-search-bar">
                <input type="text" id="projects-search" class="modal-search" placeholder="Search projects…">
            </div>
            <div class="modal-search-bar projects-filter-row">
                <select id="projects-filter" class="modal-filter" data-title="Filter projects">
                    <option value="all">All</option>
                    <option value="local">Local</option>
                    <option value="server">Server</option>
                    <option value="incognito">Incognito tabs</option>
                    <option value="peer-open">Open elsewhere</option>
                    <option value="peer-closed">Not open elsewhere</option>
                </select>
                <select id="projects-sort" class="modal-filter" data-title="Sort projects (drag a row to set a manual order)">
                    <option value="name">Name</option>
                    <option value="local">Local first</option>
                    <option value="server">Server first</option>
                    <option value="date-desc">Newest</option>
                    <option value="date-asc">Oldest</option>
                    <option value="manual">Manual order</option>
                </select>
                <select id="projects-search-mode" class="modal-filter" data-title="What the search box matches">
                    <option value="common">Name + keywords</option>
                    <option value="names">Names only</option>
                    <option value="keywords">Keywords only</option>
                </select>
            </div>
            <!-- Batch-select toolbar: appears once one or more rows are checked. -->
            <div class="projects-batch-bar" id="projects-batch-bar" style="display:none">
                <span class="projects-batch-count" id="projects-batch-count" style="display:none">0 selected</span>
                <span class="projects-batch-actions">
                    <button id="projects-select-all" class="btn-icon-text" style="display:none" data-title="Select every listed project (the current filter's rows)">${icon('check', { size: 13 })}<span>Select all</span></button>
                    <!-- The selection-only actions come and go as ONE group, so the swap is a
                         single flight instead of a button-by-button scramble (updateBatchBar). -->
                    <span class="projects-batch-selected" id="projects-batch-selected" style="display:none">
                    <button id="projects-batch-move-server" class="btn-icon-text" data-title="Move the selected local projects to a server">${icon('server', { size: 13 })}<span>Move to server</span></button>
                    <button id="projects-batch-copy-server" class="btn-icon-text" data-title="Copy the selected local projects to a server">${icon('copy', { size: 13 })}<span>Copy to server</span></button>
                    <button id="projects-batch-move-local" class="btn-icon-text" data-title="Move the selected server projects to local">${icon('download', { size: 13 })}<span>Move to local</span></button>
                    <button id="projects-batch-copy-local" class="btn-icon-text" data-title="Copy the selected server projects to local">${icon('copy', { size: 13 })}<span>Copy to local</span></button>
                    <button id="projects-batch-clear" class="btn-icon-text" data-title="Clear selection">${icon('x', { size: 13 })}<span>Clear</span></button>
                    <button id="projects-batch-remove" class="danger btn-icon-text" data-title="Remove the selected projects">${icon('trash', { size: 13 })}<span>Remove selected</span></button>
                    </span>
                </span>
            </div>
            <div class="settings-body" id="projects-list"><!-- filled by JS --></div>
            <div class="settings-footer">
                <span class="footer-hint">Projects auto-save · unopened projects expire after 7 days</span>
                <button id="projects-blank-image" class="btn-icon-text" data-title="Create a blank image to draw on">${icon('image')}<span>Blank image</span></button>
                <button id="projects-new-editor" class="btn-icon-text" data-title="Open a new empty editor in another tab">${icon('plus-circle')}<span>New editor</span></button>
                <button id="projects-clear-all" class="danger btn-icon-text" data-title="Delete every saved project">${icon('trash')}<span>Clear All</span></button>
            </div>
        </div>
    `;
}
