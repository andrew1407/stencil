import { createCollapsibleSections } from '../lib/collapsibleSections.js';
import { createSectionPeek, peekPosition, isTypingTarget } from '../lib/sectionPeek.js';
import { ASSISTANT_SECTION, SEARCH_SECTION } from '../lib/dragSections.js';
import { dragSections } from './dragWiring.js';

// Collapsible filter sections: the accordion lives in lib/collapsibleSections.js. The
// peek must send a borrowed body home BEFORE the class flips; a user toggle cancels a
// drag's queued fold-back (both wired lazily — their owners are created just below).
export const sections = createCollapsibleSections({
  doc: document,
  beforeToggle: (section) => sectionPeek.sectionToggled(section),
  onUserToggle: (id) => dragSections.manualToggle(id),
});
// The Search section owns the results: collapsing it folds the list + status too.
sections.setHook(SEARCH_SECTION, (collapsed) => document.body.classList.toggle('search-collapsed', collapsed));

// ── Alt + hover peek: a collapsed section's body in a floating mini window ───
// Hold Alt and hover a folded header (either order) to see the content WITHOUT unfolding
// the accordion. The panel borrows the REAL .section-body node (wiring intact) and returns
// it on close. Rules and timers live in lib/sectionPeek.js; this is only the DOM.
const peekPanel = document.createElement('div');
peekPanel.id = 'section-peek';
peekPanel.hidden = true;
const peekTitle = document.createElement('div');
peekTitle.className = 'peek-title';
peekPanel.appendChild(peekTitle);
document.body.appendChild(peekPanel);
let peekHome = null;   // { body, parent, next } — where the borrowed body goes back
export const sectionPeek = createSectionPeek({
  isCollapsed: (s) => !!s && !s.hidden && s.classList.contains('collapsed'),
  // Engaged = pointer inside, or a text field with typed content (a focused
  // checkbox or empty field must not pin the panel).
  isEngaged: () => {
    if (peekPanel.hidden) return false;
    if (peekPanel.matches(':hover')) return true;
    const a = document.activeElement;
    return !!a && peekPanel.contains(a) && isTypingTarget(a) && String(a.value ?? '').trim() !== '';
  },
  open: (s) => {
    const head = s.querySelector('.section-head');
    const body = s.querySelector('.section-body');
    if (!head || !body) return;
    // The assistant boots lazily on its first expand — a peek counts as one.
    if (s.id === ASSISTANT_SECTION) sections.runHook(s.id, false);
    peekHome = { body, parent: body.parentNode, next: body.nextSibling };
    peekTitle.textContent = head.querySelector('.dlbl')?.textContent || '';
    peekPanel.appendChild(body);
    peekPanel.hidden = false;
    // Measure AFTER it shows — the placement needs the panel's real size.
    const box = peekPanel.getBoundingClientRect();
    const p = peekPosition({
      anchor: head.getBoundingClientRect(),
      box: { width: box.width, height: box.height },
      viewport: { width: window.innerWidth, height: window.innerHeight },
    });
    peekPanel.style.left = `${p.left}px`;
    peekPanel.style.top = `${p.top}px`;
  },
  close: () => {
    if (peekHome) peekHome.parent.insertBefore(peekHome.body, peekHome.next);
    peekHome = null;
    peekPanel.hidden = true;
  },
});
document.addEventListener('mouseover', (e) => {
  const head = e.target.closest?.('.section-head');
  // The mouse route always peeks — gliding between headers is deliberate; only
  // the Alt KEY-press route defers to a focused text control (typing).
  if (head) { sectionPeek.enterHead(head.closest('.fsection'), e.altKey); return; }
  if (e.target.closest?.('#section-peek')) sectionPeek.enterPeek();
});
document.addEventListener('mouseout', (e) => {
  const from = e.target.closest?.('.section-head, #section-peek');
  if (!from) return;
  const to = e.relatedTarget;
  if (to && to.closest && to.closest('.section-head, #section-peek') === from) return;   // still inside
  sectionPeek.leave();
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Alt' && !e.repeat) {
    // While a text control has focus, Alt belongs to the typing (Alt+letter
    // characters, input shortcuts) — never steal it for the peek.
    if (isTypingTarget(document.activeElement)) return;
    const head = document.querySelector('.section-head:hover');
    // preventDefault keeps the bare Alt from focusing the browser's menu bar
    // while it is being used as the peek key.
    if (head) { e.preventDefault(); sectionPeek.altPressed(head.closest('.fsection')); }
  } else if (e.key === 'Escape' && sectionPeek.isOpen()) {
    e.preventDefault();
    sectionPeek.dismiss();
  }
});
// HOLD-to-peek: the panel lives only while Alt is down. Blur too — Alt+Tab
// switches away without ever delivering the keyup.
document.addEventListener('keyup', (e) => { if (e.key === 'Alt') sectionPeek.altReleased(); });
window.addEventListener('blur', () => sectionPeek.altReleased());
// A press anywhere outside the panel closes the peek (a press on a header then
// toggles that section normally — sectionToggled has already sent the body home).
document.addEventListener('pointerdown', (e) => {
  if (sectionPeek.isOpen() && !e.target.closest?.('#section-peek')) sectionPeek.dismiss();
}, true);
