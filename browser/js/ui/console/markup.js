// Markup for the console window: a header, then one field as a terminal is — the scrollback
// with the prompt as its last row — and the hidden file input a bare /upload opens.
import { icon } from '../icons.js';
import MEDIA_TYPES from '../../../../common/config/mediaTypes.json' with { type: 'json' };

export const consoleModalInner = () => `
    <div class="app-modal console-modal" data-min-w="420" data-min-h="240">
        <div class="settings-header">
            <h2>${icon('terminal', { size: 18 })} Console</h2>
            <button class="app-modal-close btn-icon-text" id="console-close">${icon('x', { size: 14 })}<span>Close</span></button>
        </div>
        <div class="console-body">
            <pre id="console-out" class="console-out" aria-live="polite"></pre>
            <div class="console-prompt-row">
                <span class="console-prompt" aria-hidden="true">></span>
                <input id="console-input" class="console-input" type="text" autocomplete="off" spellcheck="false" aria-label="Console command" placeholder="/help">
            </div>
            <input type="file" id="console-upload" accept="${MEDIA_TYPES.accept.imageOrVideo},.stencil" hidden>
        </div>
    </div>`;
