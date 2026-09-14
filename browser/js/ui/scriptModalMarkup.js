// Markup for the script window: a code editor over a highlight layer, a diagnostics strip,
// and the five actions. Kept apart from scriptModal.js so the shape reads in one piece.
import { icon } from './icons.js';

export const scriptModalInner = () => `
    <div class="app-modal script-modal">
        <div class="settings-header">
            <h2>${icon('script', { size: 18 })} Stencil Script</h2>
            <button class="app-modal-close btn-icon-text" id="script-close">${icon('x', { size: 14 })}<span>Close</span></button>
        </div>
        <div class="script-actions-bar">
            <span class="chat-settings-actions">
                <button id="script-copy" class="btn-icon-text" data-title="Copy this script to the clipboard">${icon('clipboard', { size: 14 })}<span>Copy</span></button>
                <button id="script-download" class="btn-icon-text" data-title="Download this script as stencil.stc">${icon('file-down', { size: 14 })}<span>Download</span></button>
                <input type="file" id="script-upload" accept=".stc" style="display:none;">
                <button id="script-upload-btn" class="btn-icon-text" data-title="Load a .stc file into the editor">${icon('file-up', { size: 14 })}<span>Upload</span></button>
                <button id="script-run" class="btn-icon-text primary" data-title="Run this script on the open project (Ctrl+Enter)">${icon('play', { size: 14 })}<span>Run</span></button>
                <button id="script-clear" class="btn-icon-text danger" data-title="Clear this script from the editor">${icon('trash', { size: 14 })}<span>Clear</span></button>
            </span>
        </div>
        <div class="settings-body">
            <div class="script-editor-wrap" id="script-editor-wrap">
                <pre class="script-highlight" id="script-highlight" aria-hidden="true"></pre>
                <textarea id="script-editor" class="script-input" spellcheck="false" wrap="off"
                          aria-label="Stencil script"
                          placeholder="@crop 10%&#10;@filter bw&#10;@save"></textarea>
            </div>
            <div class="script-diag" id="script-diag"></div>
        </div>
    </div>`;
