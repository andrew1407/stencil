// Jump pills over the transcript's bottom edge: ⌄ once scrolled up from the latest, ⌃
// once past the beginning (desktop chatDock parity).
import { onWindowResize, perFrame } from '../../../utils.js';
import { subscribe } from '../../../eventBus/appBus.js';
import { rowMenuLiftPx, rowMenuLiftFits, chatPopupOpen, CHAT_POPUP_EVENT } from '../view.js';

export function wireJumpPills({ transcript, jumps, jumpPills }) {
// The hovered row's "…" yields to the pills: lifts clear, or hides when the bubble is
// too short (desktop placeChatCardMore's "shift, else hide").
  let hoverRow = null;
  const clearRowMenuLift = (row) => {
    row?.style.removeProperty('--row-menu-lift');
    row?.classList.remove('chat-row-menu-yield');
  };
// The pills sit outside the scroller, so their boxes are cached; invalidated when they
// show/hide, the window resizes, or the panel changes shape.
  let pillRects = null;
  const invalidatePillRects = () => { pillRects = null; };
  onWindowResize(invalidatePillRects);
  const syncRowMenuLift = () => {
    if (!hoverRow) return;
    const btn = hoverRow.querySelector('.chat-row-menu-btn')?.getBoundingClientRect?.();
    if (!btn) return;
// A hidden pill measures 0×0 and is filtered out, so this self-clears.
    const pills = pillRects ?? (pillRects = jumpPills
      .map((b) => b?.getBoundingClientRect?.()).filter((r) => r?.width > 0));
    const lift = rowMenuLiftPx(btn, pills);
    if (!lift) { clearRowMenuLift(hoverRow); return; }
    if (rowMenuLiftFits(hoverRow.getBoundingClientRect(), btn, lift)) {
      hoverRow.style.setProperty('--row-menu-lift', `${lift}px`);
      hoverRow.classList.remove('chat-row-menu-yield');
    } else {
      hoverRow.style.removeProperty('--row-menu-lift');
      hoverRow.classList.add('chat-row-menu-yield');
    }
  };
  const syncJumps = () => {
    const max = transcript.scrollHeight - transcript.clientHeight;
// Any chat popup opens into this very corner.
    const standDown = chatPopupOpen();
    const up = transcript.scrollTop > 12 && !standDown;
    const down = max - transcript.scrollTop > 12 && !standDown;
    if (up !== jumps.classList.contains('can-up') || down !== jumps.classList.contains('can-down'))
      invalidatePillRects();
    jumps.classList.toggle('can-up', up);
    jumps.classList.toggle('can-down', down);
    syncRowMenuLift();
  };
// The pills float over the transcript, so hovering one leaves no row hovered.
  transcript.addEventListener('mouseover', (e) => {
    const row = e.target?.closest?.('.chat-msg');
// A lifted trigger sits outside its row's box, so reaching it crosses bare background;
// only a different row (or mouseleave) changes the hover.
    if (!row || !transcript.contains(row) || row === hoverRow) return;
    if (hoverRow) clearRowMenuLift(hoverRow);
    hoverRow = row;
    syncJumps();
  });
  transcript.addEventListener('mouseleave', () => {
    if (hoverRow) clearRowMenuLift(hoverRow);
    hoverRow = null;
    syncJumps();
  });
// Both edges of any chat popup, from either surface.
  subscribe(CHAT_POPUP_EVENT, syncJumps);
  transcript.addEventListener('scroll', syncJumps, { passive: true });
// A streamed reply mutates the transcript many times a frame; its geometry is read once per frame.
  new MutationObserver(perFrame(syncJumps)).observe(transcript, { childList: true, subtree: true });
  jumpPills[0].addEventListener('click', () => transcript.scrollTo({ top: 0, behavior: 'smooth' }));
  jumpPills[1].addEventListener('click', () => transcript.scrollTo({ top: transcript.scrollHeight, behavior: 'smooth' }));
  return { invalidatePillRects };
}
