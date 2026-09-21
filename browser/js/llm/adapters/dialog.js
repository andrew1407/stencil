// ── §10 window adapters: the chat, the editor's dialogs, and voice ─────────
// Opened through the very toolbar buttons the user would click, so a plan and a
// shortcut take the same path. A disabled button is a note, never a failed plan.
import UI_STRINGS from '../../config/uiStrings.json' with { type: 'json' };
import { clearSharedConversation } from '../chat/chatSession.js';

// §10 dialog: op `name` (opPlan.js) -> the toolbar button behind that window, from
// config/uiStrings.json — the very ids the hotkey actions press.
const DIALOG_BUTTON_IDS = UI_STRINGS.dialogButtonIds;
export const dialogAdapters = (app) => ({
  // §10 clearChat: the shared clear-conversation flow behind the app's own
  // confirm; the executor defers it to the plan's end. Declined = the note.
  clearChatConversation: async () => {
    const ok = await app.confirm('Clear this conversation? Its chat history will be deleted.',
      { title: 'Clear chat', danger: true, confirmIcon: 'trash' });
    if (!ok) return 'clear canceled';
    clearSharedConversation(app);
    return null;
  },
  // §10 dialog: the editor's own windows, through the very toolbar buttons the user would
  // click. A disabled button is a note, never a failed plan; null closes whatever is open.
  openDialog: async (name) => {
    if (typeof document === 'undefined') return 'no dialogs on this surface';
    if (!name) {
      const open = document.querySelectorAll('.app-modal-overlay.modal-open');
      if (!open.length) return 'no dialog is open';
      open.forEach((o) => o.classList.remove('modal-open'));
      return null;
    }
    const btn = document.getElementById(DIALOG_BUTTON_IDS[name]);
    if (!btn) return `the ${name} window is not available here`;
    if (btn.disabled) return `the ${name} window is not available right now`;
    btn.click();
    return null;
  },
  // §10 chat: the panel's own placement, through the buttons its header carries. A "dock"
  // with no "open" opens it too.
  setChatPlacement: async ({ open, dock } = {}) => {
    if (!app.chat) return 'this surface has no assistant panel';
    // Show FIRST, then place, then close: the desktop's float leg only has a window to lift once
    // the panel is showing, and both surfaces must end in the same state for the same plan.
    const show = open == null ? !!dock : open === true;
    try {
      if (show) app.chat.open();
      if (dock) app.chat.dock(dock);
      if (!show && open === false) app.chat.close();
      return null;
    } catch (err) { return err?.message || String(err); }
  },
  // §10 voiceChat: the browser-only hands-free toggle; an unsupported browser's
  // throw (or a missing coordinator) is the note, never a failed plan.
  setVoiceChat: (on) => {
    if (!app.voice) return 'voice input is not available';
    try { app.voice.voiceChat = !!on; return null; }
    catch (err) { return err?.message || String(err); }
  },
});
