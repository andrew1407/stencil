// `app.chat`, the chat panel's scripting surface (console/stencilApi.js): every call rides
// the same code path as the panel's own buttons.
import { chatLog, clearSharedConversation, peekChatController } from '../../../llm/chat/session.js';
import { rowsToMessages } from '../../../llm/chat/store.js';
import { MAX_ATTACHMENTS } from '../../../llm/chat/controller.js';
import { DOCKS } from '../geometry.js';

export function createPanelApi(deps) {
  const {
    app, ctrl, turn, setOpen, panelIsOpen, adoptLayout, setDock, renderAttachments, updateControls, voice,
  } = deps;
  return {
    open: () => setOpen(true),
    close: () => setOpen(false),
    isOpen: () => panelIsOpen(),
    dock: (mode) => {
      const m = String(mode || '').toLowerCase();
      if (!DOCKS.includes(m)) throw new Error(`Unknown dock mode "${mode}" — one of ${DOCKS.join(', ')}`);
      adoptLayout();
      setDock(m);
    },
    prompt: async (text, images = []) => {
      if (turn.isSending) throw new Error('The assistant is already answering — wait for the current turn');
// The whole batch is checked against the remaining room first, so a rejected call never
// changes the tray (§7 MAX_ATTACHMENTS).
      const room = MAX_ATTACHMENTS - (peekChatController(app)?.attachments.length || 0);
      if (images.length > room) {
        throw new Error(`up to ${MAX_ATTACHMENTS} images per message${room < MAX_ATTACHMENTS ? ` (${room} slot${room === 1 ? '' : 's'} left)` : ''}`);
      }
      for (const u of images) ctrl().addImageDataUrl(u);
      renderAttachments();
      return turn.runTurn(String(text ?? ''));
    },
// The §12.1 display form: settled turns, text only; fresh copies per read.
    history: () => rowsToMessages(chatLog()).map((m) => ({ role: m.role, text: m.text })),
    abort: () => turn.abort(),
// The trash button's shared path (§12: the persisted copy clears too); refused mid-turn.
    clear: () => {
      if (turn.isSending) throw new Error('The assistant is answering — stop the turn before clearing');
      clearSharedConversation(app);
      updateControls();
    },
    get isSending() { return turn.isSending; },
    controller: ctrl,
// Dictation into this composer — the scripting peer of the "…" item / double-click / hold.
    get voiceInput() { return !!voice()?.isOn(); },
    setVoiceInput: (on) => {
      if (on && !app.voice?.supported) throw new Error('Voice input is not supported in this browser');
      voice()?.setMode(!!on);
    },
  };
}
