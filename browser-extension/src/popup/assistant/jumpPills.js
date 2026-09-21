// ── Jump pills over the transcript's bottom edge (browser .chat-jumps parity) ──
// ⌃ while scrolled off the beginning, ⌄ while off the latest message, neither once
// the whole log fits. They win the paint order over a row's hover "…" — when the
// two would touch, the hovered bubble's trigger lifts clear of them, or (too
// little room) hides instead of sitting under them (desktop/browser parity).
import { icon } from '../../lib/icons.js';
import { msgMenuLiftPx, msgMenuLiftFits } from '../../lib/chat/msgMenu.js';

export const createJumpPills = (transcriptEl) => {
  const jumpsEl = document.getElementById('chat-jumps');
  const jumpTopBtn = document.getElementById('chat-jump-top');
  const jumpBottomBtn = document.getElementById('chat-jump-bottom');
  if (jumpTopBtn) jumpTopBtn.innerHTML = icon('chevron-up', { size: 14 });
  if (jumpBottomBtn) jumpBottomBtn.innerHTML = icon('chevron-down', { size: 14 });
  let hoverMsgEl = null;
  const clearMenuLift = (el) => {
    const btn = el?.querySelector?.('.msg-menu-btn');
    btn?.style.removeProperty('--menu-lift');
    btn?.classList.remove('msg-menu-btn-yield');
  };
  // The pills are anchored OUTSIDE the scroller, so their rects are cached — dropped on a
  // visibility flip, a resize or a re-open, never re-read per scroll tick.
  let pillRects = null;
  const livePillRects = () => (pillRects ||= [jumpTopBtn, jumpBottomBtn]
    .map((b) => b?.getBoundingClientRect?.()).filter((r) => r && r.width > 0));
  const syncRowMenuLift = () => {
    if (!hoverMsgEl) return;
    const btn = hoverMsgEl.querySelector('.msg-menu-btn');
    if (!btn) return;
    const pills = livePillRects();
    const btnRect = btn.getBoundingClientRect();
    const lift = msgMenuLiftPx(btnRect, pills);
    if (!lift) { clearMenuLift(hoverMsgEl); return; }
    if (msgMenuLiftFits(hoverMsgEl.getBoundingClientRect(), btnRect, lift)) {
      btn.style.setProperty('--menu-lift', `${lift}px`);
      btn.classList.remove('msg-menu-btn-yield');
    } else {
      btn.style.removeProperty('--menu-lift');
      btn.classList.add('msg-menu-btn-yield');
    }
  };
  const syncJumps = () => {
    if (!jumpsEl) return;
    const max = transcriptEl.scrollHeight - transcriptEl.clientHeight;
    const up = transcriptEl.scrollTop > 12;
    const down = max - transcriptEl.scrollTop > 12;
    if (up !== jumpsEl.classList.contains('can-up')
      || down !== jumpsEl.classList.contains('can-down')) pillRects = null;
    jumpsEl.classList.toggle('can-up', up);
    jumpsEl.classList.toggle('can-down', down);
    syncRowMenuLift();
  };
  window.addEventListener('resize', () => { pillRects = null; });
  // The pills float OVER the transcript, so a cursor on one leaves no row hovered —
  // hovering a pill can never hide it (browser panel.js parity).
  transcriptEl.addEventListener('mouseover', (e) => {
    const row = e.target?.closest?.('.msg');
    // A lifted trigger sits OUTSIDE its bubble's box, so reaching it crosses bare background; only
    // a different bubble (or mouseleave) may change the hover, else the lift clears mid-reach.
    if (!row || !transcriptEl.contains(row) || row === hoverMsgEl) return;
    if (hoverMsgEl) clearMenuLift(hoverMsgEl);
    hoverMsgEl = row;
    syncRowMenuLift();
  });
  transcriptEl.addEventListener('mouseleave', () => {
    if (hoverMsgEl) clearMenuLift(hoverMsgEl);
    hoverMsgEl = null;
  });
  jumpTopBtn?.addEventListener('click', () => transcriptEl.scrollTo({ top: 0, behavior: 'smooth' }));
  jumpBottomBtn?.addEventListener('click', () => transcriptEl.scrollTo({ top: transcriptEl.scrollHeight, behavior: 'smooth' }));
  transcriptEl.addEventListener('scroll', syncJumps, { passive: true });
  // Direct children only: a subtree watch made every dust-host insert cost syncJumps
  // its layout reads, and only the entry list itself changes what there is to jump to.
  new MutationObserver(syncJumps).observe(transcriptEl, { childList: true });

  // A collapsed section is display:none, so the transcript measured zero — re-read the
  // pill rects and re-sync once expanding gives it a real height.
  return { resync: () => { pillRects = null; syncJumps(); } };
};
