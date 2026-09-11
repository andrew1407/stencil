// ── Chat cards: attachments, results, the §11 choice card ───────
// Every card is built from DOM nodes with textContent — model output is data, never markup.
import { askAnswerText, sanitizeLabel } from '../llm/opPlan.js';
import { escapeHtml } from './base.js';
import { icon } from './icons.js';
import { notify } from '../utils.js';
import { wireThumbPreview } from './chatThumbPreview.js';

export const chatAttachmentStrip = (attachments) => {
  const strip = document.createElement('div');
  strip.className = 'chat-attached';
  for (const a of attachments) {
    const fig = document.createElement('span');
    fig.className = 'chat-attached-item';
    // No native title here: the hover preview below already shows the name WITH the
    // big image, and the two tooltips doubled up.
    const img = document.createElement('img');
    img.className = 'chat-attached-thumb';
    img.src = a.dataUrl;
    img.alt = a.name;
    // Small on purpose — hovering shows it big (the thumbnail alone is too small to
    // tell two screenshots apart).
    wireThumbPreview(img, a.kind === 'video' ? `${a.name} (first frame)` : a.name);
    fig.appendChild(img);
    if (a.kind === 'video') {
      const badge = document.createElement('span');
      badge.className = 'chat-attached-badge';
      badge.textContent = 'video';
      fig.appendChild(badge);
    }
    strip.appendChild(fig);
  }
  return strip;
};

// One result card: thumbnail + label + download + open-as-the-working-image.
export const chatResultCard = (r) => {
  const card = document.createElement('div');
  card.className = 'chat-result';
  const img = document.createElement('img');
  img.className = 'chat-result-thumb';
  img.src = r.dataUrl;
  img.alt = r.label;
  img.dataset.title = r.label;
  const label = document.createElement('span');
  label.className = 'chat-result-label';
  label.textContent = r.label;
  const dl = document.createElement('a');
  dl.className = 'chat-hbtn chat-result-btn';
  dl.dataset.title = `Download ${r.label}`;
  dl.download = `${sanitizeLabel(r.label)}.png`;
  dl.href = r.dataUrl;
  dl.innerHTML = icon('download', { size: 13 });
  const use = document.createElement('button');
  use.className = 'chat-hbtn chat-result-btn';
  use.dataset.title = `Open ${r.label} as the working image`;
  use.innerHTML = icon('external', { size: 13 });
  use.addEventListener('click', async () => {
    try { await window.stencil.load(r.dataUrl, { name: `${sanitizeLabel(r.label)}.png` }); }
    catch (err) { notify(`Could not open ${r.label} — ${err.message}`, 'fail'); }
  });
  card.append(img, label, dl, use);
  return card;
};

// ── §11 choice card ─────────────────────────────────────────────────────────
// The model's `ask` rendered under its reply: radios/checkboxes per mode, per-option
// previews, optional free-text, Submit disabled until something is chosen. Submitting
// sends the answer as the user's NEXT turn (§11.3); nothing here applies an edit. Every
// string is model output → textContent. Once answered the card locks — no re-firing.
export const chatAskCard = (ask, { onSubmit, previews = [] } = {}) => {
  const byIndex = new Map(previews.map((p) => [p.index, p.dataUrl]));
  const wrap = document.createElement('div');
  wrap.className = 'chat-ask';

  const q = document.createElement('div');
  q.className = 'chat-ask-q';
  q.textContent = ask.question;
  wrap.appendChild(q);

  const name = `ask-${Math.random().toString(36).slice(2)}`;   // groups the radios
  const multi = ask.mode === 'multi';
  const list = document.createElement('div');
  list.className = 'chat-ask-options';
  const inputs = [];
  ask.options.forEach((opt, i) => {
    const row = document.createElement('label');
    row.className = 'chat-ask-option';
    const box = document.createElement('input');
    box.type = multi ? 'checkbox' : 'radio';
    box.name = name;
    box.value = String(i);
    inputs.push(box);
    row.appendChild(box);
    // ONLY from `previews` — data: URLs this app rendered. An option's model-written
    // `image.url` must never reach an <img src>: that fires a request to a host the
    // MODEL chose, on render, before the user has read the card.
    const pic = byIndex.get(i);
    if (pic) {
      const img = document.createElement('img');
      img.className = 'chat-ask-thumb';
      img.src = pic;
      img.alt = '';
      row.appendChild(img);
    }
    const label = document.createElement('span');
    label.className = 'chat-ask-label';
    label.textContent = opt.label;
    row.appendChild(label);
    list.appendChild(row);
  });
  wrap.appendChild(list);

  // The custom row: picking it is what makes its input meaningful, and typing in it picks
  // it — so the two can't disagree about what will be sent.
  let customBox = null;
  let customText = null;
  if (ask.allowCustom) {
    const row = document.createElement('label');
    row.className = 'chat-ask-option chat-ask-custom';
    customBox = document.createElement('input');
    customBox.type = multi ? 'checkbox' : 'radio';
    customBox.name = name;
    customBox.value = 'custom';
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
    // Removing the button below detaches it but does NOT disarm this listener — a retained
    // reference (or assistive tech) could click it again and re-send the turn. The flag is
    // what makes "answered once" true, not the DOM.
    if (answered) return;
    const answer = askAnswerText(ask, { picked: chosen(), custom: typed() });
    if (!answer) return;
    answered = true;
    // Lock it: the card becomes a record of what was sent, not a control.
    for (const b of inputs) b.disabled = true;
    if (customText) customText.disabled = true;
    submit.remove();
    const sent = document.createElement('div');
    sent.className = 'chat-ask-sent';
    sent.textContent = answer;                    // the user's own words / picked labels
    actions.appendChild(sent);
    wrap.classList.add('chat-ask-answered');
    onSubmit?.(answer);
  });
  return wrap;
};

// Both chat surfaces read the SAME controller, so a queue change on one must repaint
// the other: whoever mutates attachments fires this. The name lives with the
// controller (chatController.js), which also fires it when a send drains the queue.

// The "Reconnect" call-to-action under an EXPIRED-session message: opens the
// connections modal, where that server's row offers the sign-in again. The URL rides
// the label so a multi-server user knows which session died.
export const chatReconnectButton = (serverUrl, onReconnect) => {
  const b = document.createElement('button');
  b.className = 'btn-icon-text chat-reconnect-cta';
  const host = String(serverUrl || '').replace(/^https?:\/\//i, '');
  b.innerHTML = icon('link', { size: 13 }) + `<span>Reconnect${host ? ` to ${escapeHtml(host)}` : ''}</span>`;
  b.addEventListener('click', () => onReconnect?.(serverUrl));
  return b;
};

// The "Configure provider" call-to-action shown under an unreachable-provider
// message: opens the LLM settings modal. Unlike the gear (which lives inside the
// composer's "…" menu and so flies from THAT trigger, per llmSettingsModal's
// originEl), this button opens itself directly, through the modal's own API, so
// the window grows out of (and gathers back into) the CTA itself — in the panel,
// where it sits right in the transcript and stays on screen through the click,
// AND in the context-menu flyout, where onBeforeOpen closes that popup first
// (a modal can't show under the menu's own grab): its rect is captured BEFORE
// that close and passed as the plain rect the shell's open() accepts.
export const chatConfigureButton = (onBeforeOpen) => {
  const cfg = document.createElement('button');
  cfg.className = 'btn-icon-text chat-config-cta';
  cfg.innerHTML = icon('gear', { size: 13 }) + '<span>Configure provider</span>';
  cfg.addEventListener('click', () => {
    const rect = cfg.getBoundingClientRect();   // captured before onBeforeOpen hides it
    onBeforeOpen?.();   // e.g. the context-menu chat closing its own popup first
    document.getElementById('chat-settings-overlay')?.__stencilModal?.open(rect);
  });
  return cfg;
};
