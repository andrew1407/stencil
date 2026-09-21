// ── What a finished turn renders ────────────────────────────────────────────
// The executed-action cards, the §11 choice card, and the reply itself. The view's
// entry builders are destructured, so every call here reads as it did inside the
// panel; `state.send` is late-bound because the ask card's Submit starts the NEXT turn.
import { editableSrc } from '../../lib/image/imageModel.js';
import { askAnswerText } from '../../llm/op/opPlan.js';
import { AUTO_DISMISS_MS } from '../../lib/chat/chatUi.js';
import { entryName } from './shared.js';

export const createResults = ({ view, getItems, state }) => {
  const { addCard, addMsg, addWarn, appendEntry } = view;

  // One renderer per executed-action card kind, keyed like the controller's op
  // executors (and opPlan.js's validators).
  const cardRenderers = {
    focus: (c) => addCard('pin', c.ok
      ? `Focused image ${c.index} (${entryName(c.entry)}) on the page`
      : `Image ${c.index} (${entryName(c.entry)}) was not found on the page`, c.ok),
    open: (c) => addCard('pencil', c.ok
      ? `Opened image ${c.index} (${entryName(c.entry)}) in the editor`
      : `Could not open image ${c.index} (${entryName(c.entry)})`, c.ok),
    attach: (c) => {
      const got = c.attached.length;
      addCard('sparkle', got
        ? `Attached image${got > 1 ? 's' : ''} ${c.attached.join(', ')} for analysis`
        : `Could not attach image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0, AUTO_DISMISS_MS);
    },
    // The glyph says which setting moved: the theme's own sun/moon/monitor (the same
    // three Options offers), and the panel icon for the list the filters govern.
    theme: (c) => addCard({ light: 'sun', dark: 'moon', system: 'monitor' }[c.mode] || 'moon',
      c.ok ? `Switched the panel to the ${c.mode} theme`
           : `Could not switch to the ${c.mode} theme`, c.ok),
    filter: (c) => addCard('sidebar', c.ok
      ? (c.applied.length ? `Filters: ${c.applied.join(' · ')}` : 'Filters unchanged')
      : 'Could not change the filters', c.ok),
    pin: (c) => {
      const got = c.pinned.length;
      addCard('pin', got
        ? `Pinned image${got > 1 ? 's' : ''} ${c.pinned.join(', ')}`
        : `Could not pin image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0);
    },
    unpin: (c) => {
      const got = c.unpinned.length;
      addCard('pin', got
        ? `Unpinned image${got > 1 ? 's' : ''} ${c.unpinned.join(', ')}`
        : `Could not unpin image${c.indices.length > 1 ? 's' : ''} ${c.indices.join(', ')}`,
      got > 0);
    },
    scanTab: (c) => addCard('monitor', c.ok
      ? `Scanned tab ${c.index}${c.title ? ` (${c.title})` : ''} — now chatting about its ${c.count} image${c.count === 1 ? '' : 's'}`
      : `Could not scan tab ${c.index}${c.title ? ` (${c.title})` : ''}`, c.ok),
    rescan: (c) => addCard('refresh', c.ok
      ? `Re-scanned the page — the listing now shows ${c.count} image${c.count === 1 ? '' : 's'}`
      : 'Could not re-scan the page', c.ok),
    accent: (c) => addCard('gear', c.ok
      ? `Accent set to ${c.applied}${c.exact ? '' : ` (the nearest preset to ${c.asked})`}`
      : `Could not set the accent to ${c.asked}`, c.ok),
    openUrl: (c) => addCard('pencil', c.ok
      ? `Opened ${c.url} in ${c.incognito ? 'a new incognito editor' : 'the editor'}`
      : `Could not open ${c.url}`, c.ok),
  };

  const renderCards = (cards) => {
    for (const c of cards) cardRenderers[c.kind]?.(c);
  };

  // §11 choice card: submitting sends the answer as the user's NEXT turn — nothing applies on
  // click, and answered cards lock. Model text is DATA: textContent throughout.
  const renderAsk = (ask) => {
    const wrap = document.createElement('div');
    wrap.className = 'chat-ask';
    const q = document.createElement('div');
    q.className = 'chat-ask-q';
    q.textContent = ask.question;
    wrap.appendChild(q);

    const name = `ask-${Math.random().toString(36).slice(2)}`;
    const multi = ask.mode === 'multi';
    const inputs = [];
    const list = document.createElement('div');
    list.className = 'chat-ask-options';
    ask.options.forEach((opt, i) => {
      const row = document.createElement('label');
      row.className = 'chat-ask-option';
      const box = document.createElement('input');
      box.type = multi ? 'checkbox' : 'radio';
      box.name = name;
      inputs.push(box);
      row.appendChild(box);
      // Only images the page scan already surfaced (the validator drops a model-supplied
      // `image.url`). A scan entry with no usable src collapses to a label-only row.
      const src = opt.image?.scanIndex != null
        ? editableSrc((getItems() || [])[opt.image.scanIndex] || {})
        : '';
      if (src) {
        const img = document.createElement('img');
        img.className = 'chat-ask-thumb';
        img.alt = '';
        img.src = src;
        img.addEventListener('error', () => img.remove());
        row.appendChild(img);
      }
      const label = document.createElement('span');
      label.className = 'chat-ask-label';
      label.textContent = opt.label;
      row.appendChild(label);
      list.appendChild(row);
    });
    wrap.appendChild(list);

    let customBox = null;
    let customText = null;
    if (ask.allowCustom) {
      const row = document.createElement('label');
      row.className = 'chat-ask-option';
      customBox = document.createElement('input');
      customBox.type = multi ? 'checkbox' : 'radio';
      customBox.name = name;
      inputs.push(customBox);
      customText = document.createElement('input');
      customText.type = 'text';
      customText.className = 'chat-ask-custom-text';
      customText.placeholder = ask.customLabel;
      customText.addEventListener('input', () => { customBox.checked = true; sync(); });
      row.append(customBox, customText);
      wrap.appendChild(row);
    }

    const actions = document.createElement('div');
    actions.className = 'chat-ask-actions';
    const submit = document.createElement('button');
    submit.type = 'button';
    submit.className = 'chat-ask-submit';
    submit.textContent = 'Submit';
    actions.appendChild(submit);
    wrap.appendChild(actions);

    const chosen = () => ask.options.filter((_, i) => inputs[i]?.checked);
    const typed = () => (customBox?.checked ? customText.value.trim() : '');
    function sync() { submit.disabled = !chosen().length && !typed(); }
    for (const b of inputs) b.addEventListener('change', sync);
    sync();

    let answered = false;
    submit.addEventListener('click', () => {
      // The flag, not the DOM, is what makes "answered once" true: a removed button keeps
      // its listener, so a retained reference could otherwise re-send the turn.
      if (answered) return;
      const answer = askAnswerText(ask, { picked: chosen(), custom: typed() });
      if (!answer) return;
      answered = true;
      for (const b of inputs) b.disabled = true;
      if (customText) customText.disabled = true;
      submit.remove();
      const sent = document.createElement('div');
      sent.className = 'chat-ask-sent';
      sent.textContent = answer;
      actions.appendChild(sent);
      wrap.classList.add('chat-ask-answered');
      state.send(answer);
    });
    appendEntry(wrap);
  };

  const renderResult = (result) => {
    renderCards(result.cards);
    addMsg('assistant', result.reply);
    // A "chat-only" reply that LOOKS like a plan is the model mangling its JSON (§1 extraction
    // found no parseable object); the raw text is the reply — data, never executed.
    if (result.chatOnly && /^\s*[{`]/.test(result.reply || '') && /"(op|actions|version)"/.test(result.reply || '')) {
      addWarn('That answer looks like a plan, but its JSON is malformed — nothing was executed. Small models often mangle plan JSON; try again or switch to a larger model in Options.');
    } else if (result.chatOnly && /^\s*</.test(result.reply || '') && /<\w+[\s>]/.test(result.reply || '')) {
      addWarn('The model answered with markup instead of a Stencil plan — nothing was executed. Try again or switch to a larger model in Options.');
    }
    for (const w of result.warnings) addWarn(w);
    if (result.ask) renderAsk(result.ask);
    if (result.continuation) {
      // attach and/or scanTab gathered context — the wording covers both.
      addMsg('note', 'Continued automatically with the gathered context…');
      renderResult(result.continuation);
    }
    // §10 clearChat rides only the OUTERMOST result and resolves after everything else, so its
    // note lands last; confirmed, the wipe itself is the feedback.
    if (result.clearChat && !result.clearChat.confirmed) {
      addMsg('note', 'Clear canceled — the conversation stays.');
    }
  };

  return { renderResult };
};
