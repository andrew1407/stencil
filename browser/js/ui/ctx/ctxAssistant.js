// The context menu's Assistant entry: gating + wiring (the flyout's chat is ctxAssistantChat.js).
import { isTouchLike, onWindowResize } from '../../utils.js';
import { loadLlmSettings } from '../../llm/llmSettings.js';
import { subscribe, EVENTS } from '../../eventBus/appBus.js';
import { assistantEnabled, assistantItemHtml } from './ctxAssistantItem.js';
import { wireCtxAssistantChat } from './ctxAssistantChat.js';

export const wireCtxAssistant = (app, host) => {
  // The flyout is not a list of .ctx-items, so none of the "activate → closeMenu()"
  // wiring applies: typing, sending and executing a plan leave the menu open.
  let assistWired = false;
  // The phone/touch fallback: the chat panel continues the very same conversation.
  const openChatPanel = () => {
    if (typeof app?.chat?.open === 'function') app.chat.open();
    else document.getElementById('chat-btn')?.click();
  };

  // Runs on every open and on LLM-settings changes, so a provider switch or a resize
  // between opens needs no reload.
  const syncAssistant = () => {
    const on = assistantEnabled(loadLlmSettings());
    let item = document.getElementById('ctx-assist-menu');
    if (on && !item) {
    // Built directly above the script window's entry, and wired like the static parents.
      const anchor = document.getElementById('ctx-script') || document.getElementById('ctx-draw-toggle');
      if (anchor) anchor.insertAdjacentHTML('beforebegin', assistantItemHtml());
      else host.menu.insertAdjacentHTML('beforeend', assistantItemHtml());
      item = document.getElementById('ctx-assist-menu');
      const sub = item?.querySelector(':scope > .ctx-sub');
      if (item && sub) host.wireSubmenu(item, sub);
    }
    if (!item) return;
    // Built once, then only hidden: the transcript survives a provider switch, and it owns
    // no separator.
    item.style.display = on ? '' : 'none';
    if (on && !assistWired) { assistWired = true; wireCtxAssistantChat(app, host, openChatPanel); }
    // Plain (no flyout) on phones and coarse pointers (the app-wide touch rule, utils.js).
    const plain = isTouchLike();
    item.dataset.noSub = plain ? '1' : '0';
    item.classList.toggle('ctx-assist-plain', plain);
    if (plain && document.getElementById('ctx-assist-sub')?.classList.contains('ctx-sub-visible')) {
      host.closeAllSubs();
    }
  };
  subscribe(EVENTS.llmSettingsChanged, syncAssistant);
  // A resize while the menu is open re-evaluates the mode.
  onWindowResize(() => { if (host.menuIsOpen()) syncAssistant(); });
  syncAssistant();

  return { syncAssistant };
};
