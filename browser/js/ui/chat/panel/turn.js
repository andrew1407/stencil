// The chat panel's turn runner: the one turn in flight, the Stop button's abort, and the
// toast a turn landing on a closed panel leaves behind.
import { notify } from '../../../utils.js';
import { loadLlmSettings } from '../../../llm/settings.js';
import { runLoggedChatTurn, closedTurnToast, chatTurnInFlight } from '../../../llm/chat/session.js';
import { chatBusyError } from '../../../llm/chat/controller.js';
import { stickToBottom, notifyAttachmentsChanged } from '../view.js';

export function createPanelTurn(deps) {
  const {
    transcript, ctrl, panelIsOpen, setOpen, markChatBusy, refreshStatus, renderAttachments, updateControls,
  } = deps;
// While a turn is in flight the send button becomes Stop and attach pauses; one turn at
// a time — send() and the facade both guard on `sending`.
  let sending = false;
  let turnAbort = null;
  const closedToast = (res) => {
    const toast = closedTurnToast(res);
    if (panelIsOpen() || !toast) return;
    notify(toast.text, toast.type, { onClick: () => setOpen(true) });
  };
// The shared logged-turn frame; runTurn rethrows a failed turn so stencil.prompt gets
// the typed rejection too.
  const runTurn = async (text) => {
    if (sending || chatTurnInFlight()) {
      notify(chatBusyError().message, 'info');
      throw chatBusyError();
    }
    sending = true;
    updateControls();
    markChatBusy(!panelIsOpen());
    const res = await runLoggedChatTurn(ctrl(), text, {
      settings: loadLlmSettings(),
// Sending is an explicit "take me to the newest": pin even if the user had scrolled up.
      begin: (abort) => { turnAbort = abort; stickToBottom(transcript); },
      onResult: (r) => {
        if (!r.ok && r.kind === 'unreachable') refreshStatus();
        closedToast(r);
      },
      cleanup: () => {
        sending = false;
        markChatBusy(false);
        turnAbort = null;
        renderAttachments();
        notifyAttachmentsChanged();
        updateControls();
        stickToBottom(transcript);
      },
    });
    if (!res.ok) throw res.error;
    return res.entry;
  };
  return {
    runTurn,
    get isSending() { return sending; },
// True when a turn was actually running.
    abort: () => {
      const had = !!turnAbort;
      turnAbort?.abort();
      return had;
    },
  };
}
