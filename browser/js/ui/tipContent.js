// ── Rich control tooltips: the content model ────────────────────────────────
// The app composes every control's tooltip as ONE `title` string (utils.js
// composeControlTitle); this parses the strings the app ALREADY writes into the desktop
// app's richer tooltip shape (heading, keycaps, rows, bullets), so no call site changes.
// The conventions it reads are the ones the titles already use:
//
//   "Fit to window (Alt+0)"          → heading + a keycap
//   "Shared server project — <url>"  → heading + a muted subtitle
//   "Drag to reorder · drag out …"   → heading + bullets
//   "None — normal editing"          → a term/description row
//   "— add an image first"           → the muted disabled-reason note
//   "(Alt+O cycles · hold Alt+Shift+O to peek)" → a muted hint, key combos highlighted
//
// Pure string in, HTML string out (everything is escaped), so it is testable under
// `node --test`. extension/src/lib/tipContent.js is a port of this file — keep the two
// rule-for-rule; tests/tipContent.test.js runs the same cases on both.

// Modifier and key vocabulary. Case-sensitive on purpose: "Delete every saved project"
// must not put a keycap on its verb, so bare words are only ever keys in a key context
// (see KEY_PROSE below) or inside a trailing "(…)" that is nothing but keys.
const MOD = 'Ctrl|Control|Cmd|Command|Meta|Win|Alt|Option|Shift';
const NAMED = 'Enter|Return|Escape|Esc|Tab|Space|Backspace|Delete|Del|Home|End|PageUp|PageDown|Arrow(?:Up|Down|Left|Right)|F\\d{1,2}';
// The key glyphs a platform may hand us instead of a word. The desktop gets these from Qt
// (QKeySequence::NativeText on macOS renders Escape as ⎋, Tab as ⇥, Page Up as ⇞ …), so the
// set has to cover what Qt actually emits or those shortcuts never become keycaps.
const KEYGLYPH = '[⌫⌦↑↓←→⎋⇥↵⌤⇞⇟↖↘␣]';
// What can end a combo: a named key, a modifier (a combo may stop on one, "Alt+Shift"), a
// lower-case gesture word the app pairs with a modifier ("Alt+click", "Shift+left-drag",
// "Alt+wheel"), a single character, a key glyph, or punctuation. The gesture word comes
// BEFORE the single character so "Alt+click" is not read as "Alt+c" plus "lick".
const ATOM = `(?:${NAMED}|${MOD}|[a-z][a-z-]{1,11}|[A-Za-z0-9]|${KEYGLYPH}|[-+=\\[\\]/\\\\.,;'\`])`;
// Nothing may run on past the key, or half a word would end up wearing a keycap.
const END = '(?![\\w-])';
// "Ctrl+Shift+Z" / "Alt+0" / "Shift+click"
const CHAINED = `(?:(?:${MOD})\\+)+${ATOM}${END}`;
// Mac display form: a run of Apple glyphs then the key ("⇧⌘Z", "⌥↑")
const GLYPHS = `[⌃⌥⇧⌘]+${ATOM}${END}`;
// A bare key word only counts as a key when the sentence is talking about keys.
const KEY_PROSE = '(?:hold|press|hit|tap|with|then|or)\\s+';

// On a Mac a modifier is drawn, not spelled — mapped at RENDER time so the same string
// reads natively on every platform. Note Ctrl → ⌃, not ⌘: a "Ctrl" that survives to here
// is a literal Control key (the registry maps its own Ctrl bindings to ⌘ beforehand).
const MAC_GLYPH = {
  Ctrl: '⌃', Control: '⌃', Alt: '⌥', Option: '⌥', Shift: '⇧',
  Cmd: '⌘', Command: '⌘', Meta: '⌘', Win: '⌘',
};
const isMacPlatform = () => {
  if (typeof navigator === 'undefined') return false;   // Node (tests) — spell them out
  const p = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
  return /mac/i.test(p || navigator.userAgent || '');
};

// ONE escaper for the whole surface — a security helper must not exist in two copies.
import { escapeHtml } from './escapeHtml.js';
export { escapeHtml };

/** Whether `s` is a key combo and nothing else (what a trailing "(…)" must be to become keycaps). */
export const isKeyCombo = s =>
  new RegExp(`^(?:${CHAINED}|${GLYPHS}|${MOD}|${NAMED}|${KEYGLYPH})$`).test(String(s).trim());

// Render one combo (already validated by isKeyCombo) as keycaps: "Ctrl+Shift+Z" →
// three <kbd>s joined by "+"; the Mac glyph form "⇧⌘Z" → one <kbd> per glyph.
export const keysHtml = (combo, mac = isMacPlatform()) => {
  const s = String(combo).trim();
  const cap = k => `<kbd class="tip-key">${escapeHtml(mac && MAC_GLYPH[k] ? MAC_GLYPH[k] : k)}</kbd>`;
  const plus = '<span class="tip-plus">+</span>';
  // Apple prints ⇧⌘S with no joiner, but a tooltip is read at a glance and a run of bare
  // glyphs looks like one symbol — so every key is separated by a "+", whichever form the
  // combo arrived in.
  if (/^[⌃⌥⇧⌘]/.test(s)) {
    const glyphs = s.match(/^[⌃⌥⇧⌘]+/)[0].split('');
    const rest = s.slice(glyphs.length);
    return [...glyphs, ...(rest ? [rest] : [])].map(cap).join(plus);
  }
  return s.split('+').filter(Boolean).map(cap).join(plus);
};

// Escape `text` and put keycaps on the key combos inside it. Combos written with "+" (or
// in Apple glyphs) are always highlighted; a lone "Shift"/"Enter" only when the prose
// says it is a key ("hold Shift") — otherwise the app's own verbs would wear keycaps.
export const highlightKeys = (text, mac = isMacPlatform()) => {
  const re = new RegExp(`(${KEY_PROSE})?(${CHAINED}|${GLYPHS}|${MOD}|${NAMED})`, 'g');
  let out = '';
  let last = 0;
  for (const m of String(text).matchAll(re)) {
    const [full, lead, token] = m;
    const chained = new RegExp(`^(?:${CHAINED}|${GLYPHS})$`).test(token);
    if (!chained && !lead) continue;  // a bare key word with no key context — leave it alone
    out += escapeHtml(String(text).slice(last, m.index)) + escapeHtml(lead || '') + keysHtml(token, mac);
    last = m.index + full.length;
  }
  return out + escapeHtml(String(text).slice(last));
};

// Split a line on the app's " · " separator, which titles use to list alternatives — but
// NOT inside parentheses: "(Alt+O cycles · hold Alt+Shift+O to peek)" is one heading
// with a hint in it, not two pieces.
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

// A secondary line reads as a sentence of its own: a lowercase fragment under the heading
// looks unfinished (user report). Only a plain lowercase first WORD is lifted — a token
// carrying a dot, slash, colon, bracket or quote is a URL, a filename or a code fragment
// ("http://…", ".stencil file", "f(x,y) …") and means what it is written as, and a lone
// word is a value, not a sentence. Term/description rows read as one sentence across the
// dash, so they are left alone.
export const sentenceCase = s => {
  const t = String(s == null ? '' : s);
  const end = t.search(/\s/);   // no second token: a VALUE (an axis letter, a filename)
  return end > 0 && /^[a-z][a-z-]*$/.test(t.slice(0, end))
    ? t[0].toUpperCase() + t.slice(1) : t;
};

// Parse a composed `title` into the tooltip's structure: {title, keys, blocks} — title/
// keys are the heading; blocks are {kind: 'row'|'bullet'|'text'|'hint'|'note', …} in
// source order.
export const parseTip = text => {
  const lines = String(text == null ? '' : text)
    .replace(/\r/g, '')
    .split('\n')
    .map(l => l.trim())
    .filter(Boolean);
  const tip = { title: '', keys: [], blocks: [] };
  if (lines.length === 0) return tip;

  // ── the shortcut ──
  // composeControlTitle appends " (combo)" then the "— reason" line, so the shortcut sits
  // on the last line that is not the reason — not necessarily the heading. Pull it off
  // whichever line carries it; a line that was nothing but the combo goes away with it.
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
  // "a · b · c" on the heading line: the first piece titles the tooltip, the rest are
  // bullets — but a single trailing piece has nothing to enumerate against, so a bullet
  // there would just be a list of one; it reads as a hint instead.
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

// Render a composed `title` as the tooltip's HTML: heading with keycaps, then rows,
// bullets, hints and the disabled-reason note. Consecutive rows share one grid and
// consecutive bullets one list, so columns line up. '' when there is nothing to show.
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
