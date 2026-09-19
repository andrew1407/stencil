// Provider + endpoint fields (llm-contract.md §5). Every endpoint is explicit user
// configuration — nothing here is ever filled from fetched content. Voice input's own
// knobs live in the Visuals & Settings modal (visualsMarkup.js) — this dialog holds
// only the provider the voice transcript is sent to.
import { icon } from './icons.js';
import UI_STRINGS from '../config/uiStrings.json' with { type: 'json' };

export const llmSettingsModalInner = () => `
        <div class="app-modal">
            <div class="settings-header">
                <h2>${icon('sparkle', { size: 18 })} Assistant</h2>
                <button class="app-modal-close btn-icon-text" id="chat-settings-close">${icon('x', { size: 14 })}<span>Close</span></button>
            </div>
            <div class="settings-body">
                <div class="vs-section">Provider</div>
                <div class="vs-row"><label data-title="Which LLM endpoint the assistant talks to">Provider</label>
                    <span class="vs-ctrl"><select id="chat-provider">
                        <option value="none">None (turned off)</option>
                        <option value="ollama">Ollama</option>
                        <option value="openai-compat">OpenAI API (LM Studio, vLLM, …)</option>
                        <option value="stencil-server">Stencil server</option>
                    </select></span>
                </div>
                <div class="vs-row" id="chat-base-url-row"><label data-title="Provider base URL">Base URL</label>
                    <span class="vs-ctrl"><input type="text" id="chat-base-url" placeholder="http://localhost:11434"></span>
                </div>
                <div class="vs-row" id="chat-model-row"><label data-title="Model name (empty = provider default) — pick a suggestion or type any name">Model</label>
                    <span class="vs-ctrl"><input type="text" id="chat-model" list="chat-model-list" placeholder="(provider default)"></span>
                    <datalist id="chat-model-list"></datalist>
                </div>
                <div class="vs-row" id="chat-api-key-row"><label data-title="Only if your endpoint requires auth — sent as 'Authorization: Bearer <key>'. Local servers (LM Studio, llama.cpp) need none; hosted OpenAI-compatible services issue keys in their account dashboard.">API key</label>
                    <span class="vs-ctrl"><input type="password" id="chat-api-key" placeholder="(optional — most local servers need none)"></span>
                </div>
                <div class="vs-row" id="chat-server-row"><label data-title="Which connected Stencil server proxies the LLM">Server</label>
                    <span class="vs-ctrl"><select id="chat-server-select"></select></span>
                </div>
                <div class="vs-row" id="chat-server-status-row">
                    <span class="conn-status conn-status-connecting" id="chat-settings-status-dot"></span>
                    <span class="chat-server-status" id="chat-server-status"></span>
                </div>
                <div class="vs-section">Chat history</div>
                <!-- Box FIRST, its label right beside it: a checkbox reads as one control, and
                     split across the row it was neither obviously a checkbox nor obviously tied
                     to that label. Same shape as the desktop's (LlmSettingsForm.cpp). -->
                <div class="vs-row vs-checks">
                    <label class="vs-inline-check" for="chat-save-chats" data-title="${UI_STRINGS.assistantSettings.saveChatsTooltip}">
                        <input type="checkbox" id="chat-save-chats"> Save chats with projects
                    </label>
                </div>
                <div class="chat-cors-note" id="chat-settings-note">
                    <div id="chat-save-chats-note">
                        ${UI_STRINGS.assistantSettings.saveChatsNote}
                    </div>
                    <div id="chat-cors-note">
                        Local providers must allow this app's origin — Ollama via
                        <code>OLLAMA_ORIGINS</code>, LM Studio via its &quot;enable CORS&quot; switch.
                    </div>
                </div>
            </div>
            <div class="settings-footer">
                <span class="footer-hint">The assistant only plans Stencil operations — endpoints come from this dialog alone.</span>
                <span class="chat-settings-actions">
                    <button id="chat-settings-cancel" class="btn-icon-text">${icon('x', { size: 14 })}<span>Cancel</span></button>
                    <button id="chat-settings-save" class="btn-icon-text primary">${icon('check', { size: 14 })}<span>Save</span></button>
                </span>
            </div>
        </div>
    `;
