// ── The context menu's Assistant entry: gating + wiring ─────────
// Shows/hides (and, when a provider is configured after load, BUILDS) the entry, and picks
// its mode. The flyout's chat wiring lives in ctxAssistantChat.js.
import { isTouchLike, onWindowResize } from '../utils.js';
import { loadLlmSettings } from '../llm/llmSettings.js';
import { subscribe, EVENTS } from '../bus/appBus.js';
import { assistantEnabled, assistantItemHtml } from './ctxAssistantItem.js';
import { wireCtxAssistantChat } from './ctxAssistantChat.js';

export const wireCtxAssistant = (app, host) => {
  // ── Assistant entry: a submenu parent whose flyout is a chat ────────────
  // The flyout is NOT a list of .ctx-items, so none of the menu's "activate →
  // host.closeMenu()" wiring applies: typing, sending, stopping and executing a plan
  // all leave the menu (and the flyout) open.
  let assistWired = false;
  // The phone/touch fallback: close the menu and open the chat panel (a full-screen
  // modal there), which continues the very same conversation.
  const openChatPanel = () => {
    if (typeof app?.chat?.open === 'function') app.chat.open();
    else document.getElementById('chat-btn')?.click();
  };

  // Show/hide (and, if the provider was configured after load, BUILD) the Assistant
  // entry, and pick its mode. Runs on every open and on LLM-settings changes, so the
  // assistant switching on/off — and a resize between opens — needs no reload.
  const syncAssistant = () => {
    const on = assistantEnabled(loadLlmSettings());
    let item = document.getElementById('ctx-assist-menu');
    if (on && !item) {
      // Built directly above the Drawing group and wired exactly like the static
      // parents (those were wired in the loop at the top of wire()).
      const anchor = document.getElementById('ctx-draw-toggle');
      if (anchor) anchor.insertAdjacentHTML('beforebegin', assistantItemHtml());
      else host.menu.insertAdjacentHTML('beforeend', assistantItemHtml());
      item = document.getElementById('ctx-assist-menu');
      const sub = item?.querySelector(':scope > .ctx-sub');
      if (item && sub) host.wireSubmenu(item, sub);
    }
    if (!item) return;
    // Built once, then only hidden — the menu session's transcript survives a provider
    // switch, and it owns no separator, so hiding it leaves the grouping as it was.
    item.style.display = on ? '' : 'none';
    if (on && !assistWired) { assistWired = true; wireCtxAssistantChat(app, host, openChatPanel); }
    // Plain (no flyout) on phones and coarse pointers: no caret, click opens the
    // panel (the app-wide touch rule, utils.js — a hover flyout needs a hover).
    const plain = isTouchLike();
    item.dataset.noSub = plain ? '1' : '0';
    item.classList.toggle('ctx-assist-plain', plain);
    if (plain && document.getElementById('ctx-assist-sub')?.classList.contains('ctx-sub-visible')) {
      host.closeAllSubs();   // a resize while open collapses the flyout instead of stranding it
    }
  };
  subscribe(EVENTS.llmSettingsChanged, syncAssistant);
  // A resize WHILE the menu is open re-evaluates the mode (and drops a flyout that
  // no longer fits) — the next open re-evaluates anyway.
  onWindowResize(() => { if (host.menuIsOpen()) syncAssistant(); });
  syncAssistant();

  return { syncAssistant };
};
