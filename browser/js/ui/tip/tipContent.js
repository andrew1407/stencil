// ── Rich control tooltips: the content model ────────────────────────────────
// Parses the ONE composed `title` string the app already writes (utils.js composeControlTitle)
// into the desktop app's richer tooltip shape — heading, keycaps, term/description rows,
// bullets, hint, disabled-reason note — so no call site changes. Pure string in, escaped HTML
// out. Byte-pinned to browser-extension/src/lib/tipContent.js; cases in tipContent.test.js.

// ONE escaper for the whole surface — a security helper must not exist in two copies.
export { escapeHtml } from '../escapeHtml.js';
import { highlightKeys, isKeyCombo, isMacPlatform, keysHtml } from './tipKeys.js';
export { isKeyCombo, keysHtml, highlightKeys };

// Split on the app's " · " separator, but NOT inside parentheses: "(Alt+O cycles · hold …)" is
// one heading with a hint in it, not two pieces.
const dotParts = (line) => {
  const s = String(line);
  const out = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < s.length; i++) {
    const ch = s[i];
    if (ch === '(') depth++;
    else if (ch === ')') depth = Math.max(0, depth - 1);
    else if (ch === '·' && depth === 0 && /\s$/.test(s.slice(0, i)) && /^\s/.test(s.slice(i + 1))) {
      out.push(s.slice(start, i));
      start = i + 1;
    }
  }
  out.push(s.slice(start));
  return out.map(p => p.trim()).filter(Boolean);
};

// Split "term — description" on the FIRST em dash (the app's own row separator).
const dashSplit = line => {
  const i = line.indexOf(' — ');
  return i === -1 ? null : { term: line.slice(0, i).trim(), desc: line.slice(i + 3).trim() };
};

// A lowercase fragment under the heading looks unfinished (user report), so a plain lowercase
// first word is lifted — but not a URL, filename or code fragment, which mean what they say.
export const sentenceCase = s => {
  const t = String(s == null ? '' : s);
  const end = t.search(/\s/);   // no second token: a VALUE (an axis letter, a filename)
  return end > 0 && /^[a-z][a-z-]*$/.test(t.slice(0, end))
    ? t[0].toUpperCase() + t.slice(1) : t;
};

// Parse a composed `title` into {title, keys, blocks}: title/keys are the heading, blocks are
// {kind: 'row'|'bullet'|'text'|'hint'|'note', …} in source order.
export const parseTip = text => {
  const lines = String(text == null ? '' : text)
    .replace(/\r/g, '')
    .split('\n')
    .map(l => l.trim())
    .filter(Boolean);
  const tip = { title: '', keys: [], blocks: [] };
  if (lines.length === 0) return tip;

  // composeControlTitle appends " (combo)" then the "— reason" line, so the shortcut sits on the
  // last line that is not the reason — not necessarily the heading.
  const isNote = l => /^[—–-]{1,2}\s+/.test(l);
  for (let i = lines.length - 1; i >= 0; i--) {
    if (isNote(lines[i])) continue;
    const paren = lines[i].match(/\s*\(([^()]*)\)\s*$/);
    // Keycaps only when every "/"-separated piece IS a key; a parenthetical of prose
    // ("(choose before adding an image)") stays part of the text.
    if (paren) {
      const parts = paren[1].split(/\s*\/\s*|\s+or\s+/).map(p => p.trim()).filter(Boolean);
      if (parts.length > 0 && parts.every(isKeyCombo)) {
        tip.keys = parts;
        const rest = lines[i].slice(0, paren.index).trim();
        if (rest) lines[i] = rest;
        else lines.splice(i, 1);
      }
    }
    break;
  }
  if (lines.length === 0) return tip;

  // ── heading ──
  let head = lines[0];
  // "a · b · c" on the heading line: the first piece titles the tooltip, the rest are bullets — but
  // a single trailing piece has nothing to enumerate against, so it reads as a hint instead.
  const headParts = dotParts(head);
  const tail = headParts.slice(1);
  head = headParts[0] || head;
  // "Shared server project — https://…" → heading + muted subtitle.
  const headRow = dashSplit(head);
  if (headRow) {
    tip.title = headRow.term;
    if (headRow.desc) tip.blocks.push({ kind: 'text', text: sentenceCase(headRow.desc), muted: true });
  } else {
    tip.title = head;
  }
  if (tail.length > 1) for (const t of tail) tip.blocks.push({ kind: 'bullet', text: sentenceCase(t) });
  else if (tail.length === 1) tip.blocks.push({ kind: 'hint', text: sentenceCase(tail[0]) });

  // ── body ──
  for (const raw of lines.slice(1)) {
    // The disabled-reason line composeControlTitle appends.
    const note = raw.match(/^[—–-]{1,2}\s+(.*)$/);
    if (note) {
      tip.blocks.push({ kind: 'note', text: sentenceCase(note[1]) });
      continue;
    }
    // A fully parenthesised line is a hint (the compare button's "(Alt+O cycles · …)").
    const wrapped = raw.match(/^\(([^()]*)\)$/);
    const line = wrapped ? wrapped[1].trim() : raw;
    const kind = wrapped ? 'hint' : 'text';
    // A bullet marker is decoration — the rows are drawn as a bulleted list anyway — so
    // strip it before deciding what the line IS.
    const bare = line.replace(/^[•*]\s+/, '');
    // A row wins over the "·" split: "Vertical split — left: original · right: current
    // edit" is ONE row whose description happens to list two halves, not two bullets.
    const row = dashSplit(bare);
    if (row && !wrapped) {
      tip.blocks.push({ kind: 'row', term: row.term, desc: row.desc });
      continue;
    }
    const parts = dotParts(bare);
    if (parts.length > 1) {
      for (const p of parts) tip.blocks.push({ kind: wrapped ? 'hint' : 'bullet', text: sentenceCase(p) });
      continue;
    }
    if (bare !== line) {
      tip.blocks.push({ kind: 'bullet', text: sentenceCase(bare) });
      continue;
    }
    tip.blocks.push({ kind, text: sentenceCase(line) });
  }
  return tip;
};

// Consecutive rows share one grid and consecutive bullets one list, so columns line up.
// '' when there is nothing to show.
export const renderTip = (text, mac = isMacPlatform()) => {
  const tip = parseTip(text);
  if (!tip.title && tip.blocks.length === 0) return '';
  let html = '';
  if (tip.title || tip.keys.length) {
    const keys = tip.keys.map(k => `<span class="tip-combo">${keysHtml(k, mac)}</span>`).join('');
    html += '<div class="tip-head">'
      + (tip.title ? `<span class="tip-title">${highlightKeys(tip.title, mac)}</span>` : '')
      + (keys ? `<span class="tip-keys">${keys}</span>` : '')
      + '</div>';
  }
  let open = '';  // the group ('rows' | 'bullets') currently being filled
  const close = () => {
    if (open === 'rows') html += '</div>';
    if (open === 'bullets') html += '</ul>';
    open = '';
  };
  for (const b of tip.blocks) {
    if (b.kind === 'row') {
      if (open !== 'rows') { close(); html += '<div class="tip-rows">'; open = 'rows'; }
      // term and desc are DIRECT children of the grid — a wrapper element would take one
      // cell and the two columns would stop lining up down the list.
      html += `<span class="tip-term">${highlightKeys(b.term, mac)}</span>`
        + `<span class="tip-desc">${highlightKeys(b.desc)}</span>`;
      continue;
    }
    if (b.kind === 'bullet') {
      if (open !== 'bullets') { close(); html += '<ul class="tip-bullets">'; open = 'bullets'; }
      html += `<li>${highlightKeys(b.text, mac)}</li>`;
      continue;
    }
    close();
    const cls = b.kind === 'note' ? 'tip-note' : (b.kind === 'hint' || b.muted ? 'tip-hint' : 'tip-line');
    html += `<div class="${cls}">${highlightKeys(b.text, mac)}</div>`;
  }
  close();
  return html;
};
