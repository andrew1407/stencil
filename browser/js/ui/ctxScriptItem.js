// The context menu's Stencil Script entry: the flyout's markup. A compact twin of the
// script window (scriptModalMarkup.js) — the same editor classes at menu scale, its own
// ctx-script-* ids, so the window and the flyout can be open at once.
import { icon } from './icons.js';

// Hung off the static #ctx-script row by ctxScript.js, never written into the menu markup.
// data-ctx-keep-tab: a code editor owns Tab (it indents), so the flyout's Tab-walks-the-
// controls navigation (ctxKeyboard.js) steps aside for it.
export const scriptFlyoutHtml = () => `
            <div class="ctx-sub ctx-script-sub" id="ctx-script-sub">
                <div class="ctx-script" id="ctx-script-pane">
                    <div class="script-editor-wrap" id="ctx-script-wrap">
                        <pre class="script-highlight" id="ctx-script-highlight" aria-hidden="true"></pre>
                        <textarea id="ctx-script-editor" class="script-input" spellcheck="false" wrap="off"
                                  aria-label="Stencil script" data-ctx-keep-tab="1"
                                  placeholder="@crop 10%&#10;@filter bw&#10;@save"></textarea>
                    </div>
                    <div class="script-diag" id="ctx-script-diag"></div>
                    <div class="ctx-script-row">
                        <span class="ctx-script-actions chat-settings-actions">
                            <button id="ctx-script-copy" class="btn-icon-text" data-title="Copy this script to the clipboard">${icon('clipboard', { size: 13 })}<span>Copy</span></button>
                            <button id="ctx-script-download" class="btn-icon-text" data-title="Download this script as stencil.stc">${icon('file-down', { size: 13 })}<span>Download</span></button>
                            <input type="file" id="ctx-script-upload" accept=".stc" style="display:none;">
                            <button id="ctx-script-upload-btn" class="btn-icon-text" data-title="Load a .stc file into the editor">${icon('file-up', { size: 13 })}<span>Upload</span></button>
                            <button id="ctx-script-run" class="btn-icon-text primary" data-title="Run this script on the open project (Ctrl+Enter)">${icon('play', { size: 13 })}<span>Run</span></button>
                            <button id="ctx-script-clear" class="btn-icon-text danger" data-title="Clear this script from the editor">${icon('trash', { size: 13 })}<span>Clear</span></button>
                        </span>
                    </div>
                </div>
            </div>`;
