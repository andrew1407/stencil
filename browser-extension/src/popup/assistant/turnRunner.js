// ── The send loop ───────────────────────────────────────────────────────────
// One turn, start to finish: drain the tray, put the user's message up, run the
// controller behind the stop button, render what came back — or a failed bubble with
// Retry (and Configure, when the provider itself is what is wrong).
import { icon } from '../../lib/icons.js';
import { setTip } from '../../lib/tip/tip.js';
import { LlmError } from '../../llm/llmClient.js';
import { turnFailureText, isUnreachableError } from '../../llm/llmSurface.js';

export const createTurnRunner = ({ sendBtn, inputEl, view, tray, renderResult, state }) => {
  const { pending, renderTray, syncClearBtn } = tray;
  const { addMsg, addAttachments, addRetry, addConfigureCta, addThinking,
          clearConversation, hideSuggestions, msgMeta } = view;

  const setBusy = (b) => {
    state.busy = b;
    // While a turn runs the send button IS the stop button (browser parity).
    sendBtn.disabled = false;
    sendBtn.innerHTML = icon(b ? 'stop' : 'send', { size: 15 });
    setTip(sendBtn, b ? 'Stop the response' : 'Send', { label: true });
    inputEl.disabled = b;
    syncClearBtn();
  };

  // `preset` is an answer submitted by a §11 choice card rather than typed — it must not
  // consume (or clear) whatever the user has half-written in the composer.
  const send = async (preset, requeue = []) => {
    if (state.busy) { state.turnAbort?.abort(); return; }   // send button = STOP mid-turn
    const typedIn = preset != null;
    const text = typedIn ? String(preset).trim() : inputEl.value.trim();
    // A retry re-queues its turn's drained attachments (never over new ones).
    if (!pending.length && requeue.length) pending.push(...requeue);
    if (!text && !pending.length) return;
    hideSuggestions();
    if (!typedIn) inputEl.value = '';
    const attachments = pending.splice(0);
    renderTray();
    // The attached images belong to the USER's turn: thumbnails on the user's side,
    // above their message (assistant-side cards would read as if the assistant said it).
    if (attachments.length) addAttachments(attachments);
    const userEl = addMsg('user', text || '(attached images)');
    // Resend re-sends the ORIGINAL turn — its typed text (not the placeholder) and
    // its drained attachments, which stay reachable here exactly like Retry's.
    msgMeta.set(userEl, { text: text || '(attached images)', resendText: text, attachments });
    setBusy(true);
    const thinking = addThinking();
    try {
      state.turnAbort = typeof AbortController !== 'undefined' ? new AbortController() : null;
      const result = await state.controller.send(text, { attachments, signal: state.turnAbort?.signal });
      thinking.remove();
      renderResult(result);
    } catch (err) {
      thinking.remove();
      if (err?.name === 'AbortError') addRetry(addMsg('error', 'Stopped.'), text, send, attachments);
      else if (err instanceof LlmError && err.kind === 'truncated') addMsg('error', err.message);
      else if (err instanceof LlmError && err.kind === 'refusal') addMsg('error', `The model refused: ${err.message}`);
      else if (err instanceof LlmError && err.kind === 'disabled') addMsg('error', 'The assistant is not enabled on this server (no API key configured).');
      else {
        // turnFailureText is the browser's error voice, the same words on every surface. Nothing is
        // auto-retried, and a Stop never offers a retry.
        const el = addMsg('error', turnFailureText(state.llmSettings, err));
        // The provider itself is the problem: offer Configure ABOVE Retry (browser
        // chatConfigureButton parity — its card renders the CTA before the retry icon).
        if (isUnreachableError(err)) addConfigureCta(el);
        // Icon-only (the composer buttons' shape) — a labelled button inside the
        // bubble reads as part of the message.
        addRetry(el, text, send, attachments);
      }
    } finally {
      state.turnAbort = null;
      setBusy(false);
      // §10 clearChat, confirmed: tear the conversation down through the existing
      // clear flow — this turn included — now that busy has dropped.
      if (state.wipeAfterTurn) {
        state.wipeAfterTurn = false;
        clearConversation();
      }
      inputEl.focus();
    }
  };

  return { setBusy, send };
};
