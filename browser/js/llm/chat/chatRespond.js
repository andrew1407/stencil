// The model round behind a chat turn (llm-contract.md §7): the wire copy's edge map, the
// text-only retry, plan execution, the auto-continuation, and the §10 deferred flush at the end.
import { EDITOR_SYSTEM_PROMPT, parseOpPlan, executeOpPlan, renderAskPreviews } from '../plan/opPlan.js';
import { CONTINUATION_NOTE } from './chatStore.js';
import { EDGE_MAP_SENTENCE, attachmentSaveName, isImageRejection, planEditsTheImage,
  planLoadsWithoutTracing, replayMessages, splitDataUrl } from './chatTurn.js';

// `state` is the controller's own mutable turn state (attachments, video binding, the text-only
// latch); `caps` is the §10 capability bag every op execution replays.
export const createResponder = ({ stencil, state, history, pushHistory, edgeOf, snapshot,
  getClient, loadImage, frameAt, exportImage, previewThumb, savedServers, openIncognito, caps }) => {
  const { clearChatConversation, openDialog, saveProject } = caps;

  // The §4 prompt + §10 settings block plus a short dynamic suffix (§4 allows appending). The
  // §7 edge-map sentence rides the suffix only when this turn attached the edge map.
  const buildSystem = (withEdgeMap = false) => {
    let s = EDITOR_SYSTEM_PROMPT;
    const size = stencil?.imageSize;
    if (size) s += `\n\nCurrent working image: ${size.width}x${size.height} px.`;
    if (state.videoInput) s += `\nThe current input is a video (${state.videoInput.name}) — "frame" ops are valid.`;
    if (withEdgeMap) s += `\n${EDGE_MAP_SENTENCE}`;
    return s;
  };

  // §10 clearChat and `dialog` land at the turn's true END — after the §7 continuation round and
  // the §3 passes. Both rounds feed one `deferred` list, deduped by op.
  const flushDeferred = async (deferred, warnings) => {
    if (!deferred.length) return;
    // One action per deferred op, and it is the LAST one asked for: a plan that closes one window
    // and opens another ends with the second one open.
    const byOp = new Map();
    for (const a of deferred) byOp.set(a.op, a);
    const actions = [...byOp.values()];
    // Every DEFERRED op's capability rides along — a replay missing one would fail the
    // op at the very end of a turn that had otherwise gone through.
    const { warnings: w } = await executeOpPlan({ actions, variants: [], warnings: [] }, stencil,
      { clearChatConversation, openDialog });
    warnings.push(...w);
  };

  // One model round against the current history. Split out of send() so an
  // auto-continuation (§7) can run a second one with the freshly loaded image in place.
  const respond = async ({ signal, preWarnings, autoAttached, continued, edgeImage = null, deferred = [] }) => {
    const client = getClient();
    const messages = replayMessages(history);
    // §7 edge map: spliced into the WIRE copy only, directly after the working snapshot (images[0]).
    // History never holds it, so the replay rule keeps picking a real snapshot/attachment.
    const withEdgeMap = !!edgeImage && !state.textOnlyModel;
    if (withEdgeMap) {
      const last = messages[messages.length - 1];
      last.images = [last.images[0], edgeImage, ...last.images.slice(1)];
    }

    let raw;
    try {
      raw = await client.chat({ system: buildSystem(withEdgeMap), messages, signal });
    } catch (err) {
      // A text-only model rejects the auto-attached snapshot: stop attaching it for the session,
      // strip the images out of the replayed history, and retry ONCE.
      if (!autoAttached || !isImageRejection(err)) throw err;
      state.textOnlyModel = true;
      for (const m of history) delete m.images;
      preWarnings.push('this model is text-only, so the picture was not sent — pick a vision model to ask about the image itself');
      raw = await client.chat({ system: buildSystem(), messages: replayMessages(history), signal });
    }
    // Replay the RAW model text as the assistant turn so the model keeps answering
    // in pure-JSON form; the parsed `reply` is what the user sees.
    pushHistory({ role: 'assistant', text: raw });

    const plan = parseOpPlan(raw);
    if (plan.chatOnly && /^\s*[{`]/.test(raw) && /"(op|actions|version)"/.test(raw)) {
      plan.warnings.push('That answer looks like a plan, but its JSON is malformed — nothing was executed. Retry, or switch to a larger model.');
    } else if (plan.chatOnly && /^\s*</.test(raw) && /<\w+[\s>]/.test(raw)) {
      plan.warnings.push('The model answered with markup instead of a Stencil plan — nothing was executed. Retry, or switch to a larger model.');
    }
    // An EDITING plan adopts the just-attached picture only when the editor is EMPTY; with an
    // image already open the attachment stays a reference (§7).
    if (!autoAttached && state.turnAttachments.length && planEditsTheImage(plan)) {
      const adopt = state.turnAttachments[0];
      try {
        await loadImage(adopt.dataUrl, adopt.name);
        preWarnings.push(`opened ${adopt.name} in the editor first — the actions ran on it`);
      } catch {
        // Loading failed: fall through and let the ops report their own trouble.
      }
    }
    const loadFrame = (state.videoInput && frameAt)
      ? async (idx) => { await loadImage(await frameAt(state.videoInput.file, idx), `frame${idx}`); }
      : null;
    // §10 openUrl guard: the pool of URLs the model may echo — the user's OWN
    // messages this conversation (assistant/replayed text never counts).
    const userText = () => history.filter((m) => m.role === 'user').map((m) => m.text).join('\n');
    // §2.1: the turn's attachments, 1-based, as the `image` op indexes them — the same list the
    // empty-canvas adoption draws from.
    let activeAttachment = state.turnAttachments.length === 1 ? state.turnAttachments[0] : null;
    const loadAttachment = async (index) => {
      const at = state.turnAttachments[index - 1];
      if (!at) throw new Error(`this message attached ${state.turnAttachments.length} image(s)`);
      await loadImage(at.dataUrl, at.name);
      activeAttachment = at;
    };
    const { results, warnings } = await executeOpPlan(plan, stencil, {
      ...caps, exportImage, loadFrame, savedServers, userText, openIncognito, loadAttachment,
      deferredSink: deferred,
      saveProject: saveProject
        ? (name) => saveProject(name || attachmentSaveName(activeAttachment))
        : null,
    });
    if (!continued && planLoadsWithoutTracing(plan) && snapshot && !state.textOnlyModel && stencil?.imageSize) {
      // Wire-only (§7): the persistence layer owns this string and refuses to store
      // it, so a restored transcript can never show the machinery (§12.1).
      const note = { role: 'user', text: CONTINUATION_NOTE };
      let nextEdge = null;
      try {
        const url = await snapshot();
        const img = splitDataUrl(url);
        if (img) {
          note.images = [img];
          nextEdge = await edgeOf(url);   // fresh edge map for the fresh image, wire-only
        }
      } catch { /* no snapshot → the note alone still moves the turn along */ }
      if (note.images) {
        pushHistory(note);
        const next = await respond({ signal, preWarnings, autoAttached: true, continued: true, edgeImage: nextEdge, deferred });
        // The continuation round finished — NOW the turn is over: run what both
        // rounds deferred, folding its notes into the merged warnings.
        const combined = warnings.concat(next.warnings);
        await flushDeferred(deferred, combined);
        return { ...next, warnings: combined, results: results.concat(next.results) };
      }
    }
    // §3.0: the turn ends HERE — no post-plan model round follows the reply. §11 option previews
    // render AFTER the edits ran, and against a copy, so the question leaves the image untouched.
    const { previews, warnings: askWarnings } =
      await renderAskPreviews(plan.ask, stencil, { exportImage, loadFrame });
    // The card shows small thumbs only — never store the full-resolution exports.
    const askPreviews = [];
    for (const p of previews) askPreviews.push({ ...p, dataUrl: await previewThumb(p.dataUrl) });
    // Outermost round only — a continued round hands its deferred actions back
    // to the caller, which flushes them once the whole turn is over.
    if (!continued) await flushDeferred(deferred, warnings);
    return {
      reply: plan.reply, warnings: preWarnings.concat(warnings, askWarnings), results,
      ask: plan.ask, askPreviews, chatOnly: plan.chatOnly,
    };
  };
  return respond;
};
