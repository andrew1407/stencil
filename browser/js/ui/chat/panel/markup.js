// The assistant chat panel's markup: header and dock buttons, transcript, jump pills, the
// composer and the float handles. Split out of ui/chat/panel.js, which wires it.
import { icon } from '../../icons.js';
import { chatSuggestionsHtml, chatDropCueHtml, chatComposerActionsHtml } from '../view.js';

export function chatPanelInner() {
    return `
        <div class="chat-header" id="chat-header">
            <span class="chat-title" id="chat-title">${icon('sparkle', { size: 14 })}<span class="chat-title-text">Assistant</span></span>
            <span class="chat-dock-btns">
                <button id="chat-dock-left-btn" class="chat-hbtn chat-dock-btn" data-dock="left" data-title="Dock left — or drag the header to an edge">${icon('chevron-left', { size: 13 })}</button>
                <button id="chat-dock-top-btn" class="chat-hbtn chat-dock-btn" data-dock="top" data-title="Dock top — or drag the header to an edge">${icon('chevron-up', { size: 13 })}</button>
                <button id="chat-dock-bottom-btn" class="chat-hbtn chat-dock-btn" data-dock="bottom" data-title="Dock bottom — or drag the header to an edge">${icon('chevron-down', { size: 13 })}</button>
                <button id="chat-dock-right-btn" class="chat-hbtn chat-dock-btn" data-dock="right" data-title="Dock right — or drag the header to an edge">${icon('chevron-right', { size: 13 })}</button>
                <button id="chat-float-btn" class="chat-hbtn chat-dock-btn" data-dock="float" data-title="Float — drag the header to move">${icon('maximize', { size: 13 })}</button>
            </span>
            <button id="chat-close" class="chat-hbtn" data-title="Close assistant">${icon('x', { size: 13 })}</button>
        </div>
        <div class="chat-transcript" id="chat-transcript">
            <div class="chat-empty" id="chat-empty">
                ${chatSuggestionsHtml()}
            </div>
        </div>
        <div class="chat-jumps" id="chat-jumps">
            <button id="chat-jump-top" class="chat-jump-btn" data-title="Jump to the beginning">${icon('chevron-up', { size: 14 })}</button>
            <button id="chat-jump-bottom" class="chat-jump-btn" data-title="Jump to the latest message">${icon('chevron-down', { size: 14 })}</button>
        </div>
        <div class="chat-attachments" id="chat-attachments"></div>
        <div class="chat-input-sizer" id="chat-input-sizer"></div>
        <div class="chat-input-row" id="chat-input-row">
            <div class="chat-input-wrap" id="chat-input-wrap">
                ${chatDropCueHtml()}
                <textarea id="chat-input" rows="2" placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"></textarea>
            </div>
            ${chatComposerActionsHtml({
    prefix: 'chat',
    actionsClass: 'chat-input-actions',
    gearClass: 'chat-config-btn',
    // Attach, Clear and Settings all live in the shared "…" menu now.
  })}
        </div>
        <div class="chat-resizer" id="chat-resizer"></div>
        <div class="chat-float-handle chat-float-handle-n"  data-dir="n"></div>
        <div class="chat-float-handle chat-float-handle-s"  data-dir="s"></div>
        <div class="chat-float-handle chat-float-handle-e"  data-dir="e"></div>
        <div class="chat-float-handle chat-float-handle-w"  data-dir="w"></div>
        <div class="chat-float-handle chat-float-handle-ne" data-dir="ne"></div>
        <div class="chat-float-handle chat-float-handle-nw" data-dir="nw"></div>
        <div class="chat-float-handle chat-float-handle-sw" data-dir="sw"></div>
    `;
}
