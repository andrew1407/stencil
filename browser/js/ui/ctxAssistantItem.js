// The context menu's Assistant entry: gating + markup. A compact chat on the same
// conversation as the panel (js/llm/chatSession.js).
import { icon } from './icons.js';
import { ctxArrow } from './ctxArrow.js';
import { chatSuggestionsHtml, chatComposerActionsHtml } from './chatView.js';

// Exists only when a provider is configured; an unreachable one still shows it.
export const assistantEnabled = (settings) => (settings?.provider ?? 'none') !== 'none';

// Sits directly above the drawing items with no separator of its own, so gating it off
// leaves the separator set untouched.
export const assistantItemHtml = () => `
        <!-- Assistant submenu: the flyout is a compact chat (chat panel's conversation) -->
        <div class="ctx-item" id="ctx-assist-menu">
            <span class="ctx-icon">${icon('sparkle')}</span><span class="ctx-label">Assistant</span><span class="ctx-hotkey" data-hk="toggleChat">Alt+G</span>${ctxArrow(true)}
            <div class="ctx-sub ctx-assist-sub" id="ctx-assist-sub">
                <div class="ctx-assist" id="ctx-assist">
                    <div class="ctx-assist-transcript" id="ctx-assist-transcript">
                        <div class="chat-empty">
                            ${chatSuggestionsHtml()}
                        </div>
                    </div>
                    <div class="ctx-assist-attachments" id="ctx-assist-attachments"></div>
                    <div class="ctx-assist-row">
                        <div class="ctx-assist-inputcol">
                            <div class="ctx-assist-sizer" id="ctx-assist-sizer"></div>
                            <textarea id="ctx-assist-input" rows="2" placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"></textarea>
                        </div>
                        ${chatComposerActionsHtml({
    prefix: 'ctx-assist',
    actionsClass: 'ctx-assist-actions',
    gearClass: 'ctx-assist-config',
    gearTitle: 'Assistant settings — provider &amp; model',
  })}
                    </div>
                </div>
            </div>
        </div>`;
