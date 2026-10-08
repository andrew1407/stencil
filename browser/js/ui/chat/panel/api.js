// `app.chat`, the chat panel's scripting surface (console/stencilApi.js): every call rides
// the same code path as the panel's own buttons.
import { chatLog, clearSharedConversation, peekChatController, chatTurnInFlight } from '../../../llm/chat/session.js';
import { rowsToMessages } from '../../../llm/chat/store.js';
import { MAX_ATTACHMENTS, chatBusyError } from '../../../llm/chat/controller.js';
import { DOCKS, DOCK_SIDES } from '../geometry.js';

export function createPanelApi(deps) {
  const {
    app, ctrl, turn, setOpen, panelIsOpen, adoptLayout, setDock, floatAt, openFrom, renderAttachments,
    updateControls, voice,
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
// Docked on a side, else floating with its top-left corner on a client point: the user's own layout,
// as a header drag's is, and an open panel moves there. A float forms out of `from`, a client rect.
    openAt: (spot, { from = null } = {}) => {
      const side = typeof spot === 'string' ? spot.toLowerCase() : null;
      if (side !== null && !DOCK_SIDES.includes(side)) {
        throw new Error(`Unknown dock side "${spot}" — one of ${DOCK_SIDES.join(', ')}`);
      }
      if (side === null && !(Number.isFinite(spot?.x) && Number.isFinite(spot?.y))) {
        throw new Error('openAt needs a dock side or a client point');
      }
      adoptLayout();
      if (side === null) floatAt(spot.x, spot.y);
      openFrom(from, () => {
        setDock(side ?? 'float');
        if (!panelIsOpen()) setOpen(true);
      });
    },
    prompt: async (text, images = []) => {
      if (turn.isSending || chatTurnInFlight()) throw chatBusyError();
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
      if (turn.isSending || chatTurnInFlight()) throw new Error('The assistant is answering — stop the turn before clearing');
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
