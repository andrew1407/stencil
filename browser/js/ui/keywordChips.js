import { FILTER_ENTERING_CLASS, disintegrate, flipFrom, leaveThenRemove,
         motionReduced, speckPainter,
         CHIP_MOTE_PX, CHIP_DUST_MS, CHIP_DUST_DRIFT, chipGrid } from './motion.js';
import { icon } from './icons.js';

// One keyword list, edited as chips. The list is the value; the input above it only
// proposes words. Desktop twin: dialogs/KeywordChips.{hpp,cpp}.

// The chip's own clocks, all twinned in KeywordChipsMotion.cpp and the projectMeta.css
// keyframes. The mote size, clock, throw and grid are the shared chip recipe
// (motion/tiles.js), so anything else that should read like a chip flies on the same one.
const CHIP_LEAVE_MS = CHIP_DUST_MS;
const ENTER_DELAY_MS = 285;
const CHIP_ENTER_MS = 510;
// Element-wise: a keyword may hold a comma, which would make ['a,b'] and ['a','b'] equal.
const sameOrder = (a, b) => a.length === b.length && a.every((w, i) => w === b[i]);

// ONE keyword, however many words it has: trimmed, inner whitespace collapsed, lowercased.
// "kitchen remodel" is a keyword, not two. '' when nothing was typed.
export const normalizeKeyword = (raw) =>
  String(raw ?? '').trim().replace(/\s+/g, ' ').toLowerCase();

// A stored list, cleaned and de-duplicated in order (the store's own setKeywords rule).
export const parseKeywords = (list) => {
  const out = [];
  for (const raw of Array.isArray(list) ? list : [list]) {
    const k = normalizeKeyword(raw);
    if (k && !out.includes(k)) out.push(k);
  }
  return out;
};

// One already held MOVES to the front rather than doubling, so a re-add is never a no-op.
export const addKeywords = (list, raw) => {
  const k = normalizeKeyword(raw);
  if (!k) return list.slice();
  const next = list.filter((w) => w !== k);
  next.unshift(k);
  return next;
};

export const keywordChipsField = ({ placeholder }) => ({
  markup: (name) => `
                <div class="kw-add">
                    <input type="text" id="${name}-input" class="confirm-prompt-input" placeholder="${placeholder}" aria-label="Add a keyword">
                    <button id="${name}-add" class="btn-icon-text primary">${icon('plus', { size: 14 })}<span>Add</span></button>
                </div>
                <div class="kw-chips" id="${name}-chips" role="list"></div>`,

  // In the footer beside Cancel, with the window's other verbs — never over the well.
  footer: (name) =>
    `<button id="${name}-clear" class="btn-icon-text danger">${icon('x', { size: 14 })}<span>Clear all</span></button>`,

  wire: (name) => {
    const $ = (id) => document.getElementById(id);
    const input = $(`${name}-input`);
    const chips = $(`${name}-chips`);
    const clearBtn = $(`${name}-clear`);
    let list = [];
    let laidOut = [];   // the order the row currently holds, so a removal need not restack
// Patched in place, never rebuilt: a rebuild would delete a chip mid-leave and leave every
// survivor a new node with no box to fly from.
    const nodeFor = new Map();

    const makeChip = (word) => {
      const chip = document.createElement('span');
      chip.className = 'kw-chip';
      chip.setAttribute('role', 'listitem');
      const label = document.createElement('span');
      label.className = 'kw-chip-name';
      label.textContent = word;
      const rm = document.createElement('button');
      rm.className = 'kw-chip-x';
      rm.setAttribute('aria-label', `Remove ${word}`);   // no data-title: the word says it
      rm.innerHTML = icon('x', { size: 12 });
      rm.addEventListener('click', () => { drop(word); input.focus(); });
      chip.append(label, rm);
      return chip;
    };

// A chip mid-arrival, finished at once. Safe on one that never had a flight.
    const settle = (el) => {
      if (!el?.classList?.contains(FILTER_ENTERING_CLASS)) return;
      clearTimeout(el.__settle);
      el.__settle = null;
      el.classList.remove(FILTER_ENTERING_CLASS);
      if (el.style) el.style.animationDelay = '';
    };

// The list is the truth; this brings the DOM to it. A word that went scatters and collapses
// its own width, a word that arrived dusts in, and every survivor FLIPs from where it was.
    const render = () => {
      clearBtn.disabled = list.length === 0;
      const still = new Set(list);
      const quiet = motionReduced();
      const was = new Map();
      if (!quiet) for (const [w, el] of nodeFor) was.set(w, el.getBoundingClientRect());

// Clear all sends every chip at once, sharing the one mote budget (chipGrid) so a long
// list does not spawn a cloud per chip and stutter.
      const going = [...nodeFor].filter(([w]) => !still.has(w));
      going.forEach(([w, el]) => {
        nodeFor.delete(w);
        if (quiet) { el.remove(); return; }
        leaveThenRemove(el, () => el.remove(), {
          ms: CHIP_LEAVE_MS, dustMs: CHIP_DUST_MS, drift: CHIP_DUST_DRIFT,
          px: CHIP_MOTE_PX, ...chipGrid(going.length),
        });
      });
      const arrived = [];
      for (const word of list)
        if (!nodeFor.has(word)) { const el = makeChip(word); nodeFor.set(word, el); arrived.push(el); }
// Re-appending an existing node MOVES it, so only restack when the ORDER really changed —
// an add, or a re-add pulling a word to the front. On a plain removal the survivors keep
// their places; the chips after the gap still slide, pulled by its collapse.
      const shown = laidOut.filter((w) => nodeFor.has(w));
      if (arrived.length || !sameOrder(shown, list))
        for (const word of list) chips.appendChild(nodeFor.get(word));
      laidOut = [...list];
      if (quiet) return;
// Typing fast lands a chip while the one before it is still behind its own hold, so each
// arrival settles whatever is mid-flight rather than leaving the row full of gaps.
      if (arrived.length) for (const el of [...chips.children]) settle(el);
// With chips leaving, THEIR collapse slides the survivors; a FLIP too would race it.
      if (!going.length) for (const [w, from] of was) {
        const el = nodeFor.get(w);
        if (el) flipFrom(el, from);
      }
// Held back until most of the cloud has landed, or it appears and the dust trails it.
      arrived.forEach((el) => {
        const dust = disintegrate(el, {
          gather: true, ms: CHIP_DUST_MS, drift: CHIP_DUST_DRIFT, px: CHIP_MOTE_PX,
          toBody: true, hostClass: 'dust-forming', paintTile: speckPainter(el),
        });
        el.style.animationDelay = dust ? `${ENTER_DELAY_MS}ms` : '0ms';
        el.classList.add(FILTER_ENTERING_CLASS);
        el.__settle = setTimeout(() => settle(el), ENTER_DELAY_MS + CHIP_ENTER_MS + 60);
      });
    };

    const drop = (word) => { list = list.filter((k) => k !== word); render(); };

    const add = () => {
      const next = addKeywords(list, input.value);
      input.value = '';
      if (next.length !== list.length || next[0] !== list[0]) { list = next; render(); }
      input.focus();
    };
    $(`${name}-add`).addEventListener('click', add);
    clearBtn.addEventListener('click', () => {
      list = [];
      input.value = '';   // else Save would put the typed word straight back
      render();
      input.focus();
    });
    // Enter adds the word; it never saves the window, or a half-typed keyword would be lost.
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); add(); }
    });

    return {
      set: (value) => { list = parseKeywords(value); render(); },
      // Whatever is still typed counts as named — Save never drops it on the floor.
      get: () => addKeywords(list, input.value),
      focus: () => input.focus(),
      reset: () => { input.value = ''; },   // closing discards what was typed, never added
    };
  },
});
