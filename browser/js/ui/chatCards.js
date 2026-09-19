// Chat cards. Every card is built from DOM nodes with textContent — model output is data, never markup.
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
// No native title: the hover preview already shows the name.
    const img = document.createElement('img');
    img.className = 'chat-attached-thumb';
    img.src = a.dataUrl;
    img.alt = a.name;
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

// The model's `ask` (§11) under its reply; submitting sends the answer as the user's next
// turn (§11.3). Every string is model output → textContent. Answered once, then locked.
export const chatAskCard = (ask, { onSubmit, previews = [] } = {}) => {
  const byIndex = new Map(previews.map((p) => [p.index, p.dataUrl]));
  const wrap = document.createElement('div');
  wrap.className = 'chat-ask';

  const q = document.createElement('div');
  q.className = 'chat-ask-q';
  q.textContent = ask.question;
  wrap.appendChild(q);

  const name = `ask-${Math.random().toString(36).slice(2)}`;
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
// Only from `previews` (data: URLs this app rendered): a model-written `image.url` must
// never reach an <img src>.
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

// Picking the custom row makes its input meaningful, and typing in it picks it.
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
// Removing the button does not disarm this listener; the flag is what makes "answered once" true.
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
    onSubmit?.(answer);
  });
  return wrap;
};

// Under an expired-session message: opens the connections modal for that server.
export const chatReconnectButton = (serverUrl, onReconnect) => {
  const b = document.createElement('button');
  b.className = 'btn-icon-text chat-reconnect-cta';
  const host = String(serverUrl || '').replace(/^https?:\/\//i, '');
  b.innerHTML = icon('link', { size: 13 }) + `<span>Reconnect${host ? ` to ${escapeHtml(host)}` : ''}</span>`;
  b.addEventListener('click', () => onReconnect?.(serverUrl));
  return b;
};

// Opens the LLM settings modal through the modal's own API so the window flies from the CTA;
// the rect is captured before onBeforeOpen closes the context-menu flyout.
export const chatConfigureButton = (onBeforeOpen) => {
  const cfg = document.createElement('button');
  cfg.className = 'btn-icon-text chat-config-cta';
  cfg.innerHTML = icon('gear', { size: 13 }) + '<span>Configure provider</span>';
  cfg.addEventListener('click', () => {
    const rect = cfg.getBoundingClientRect();
    onBeforeOpen?.();
    document.getElementById('chat-settings-overlay')?.__stencilModal?.open(rect);
  });
  return cfg;
};
