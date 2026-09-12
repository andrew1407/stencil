import { createCollapsibleSections } from '../lib/collapsibleSections.js';
import { createSectionPeek, peekPosition, isTypingTarget } from '../lib/sectionPeek.js';
import { ASSISTANT_SECTION, SEARCH_SECTION } from '../lib/dragSections.js';
import { dragSections } from './dragWiring.js';

// The peek must send a borrowed body home BEFORE the class flips; a user toggle cancels
// a drag's queued fold-back. Both are wired lazily: their owners are created below.
export const sections = createCollapsibleSections({
  doc: document,
  beforeToggle: (section) => sectionPeek.sectionToggled(section),
  onUserToggle: (id) => dragSections.manualToggle(id),
});
sections.setHook(SEARCH_SECTION, (collapsed) => document.body.classList.toggle('search-collapsed', collapsed));

// Alt + hover peek: the panel borrows the REAL .section-body node (wiring intact) and
// returns it on close. Rules and timers live in lib/sectionPeek.js.
const peekPanel = document.createElement('div');
peekPanel.id = 'section-peek';
peekPanel.hidden = true;
const peekTitle = document.createElement('div');
peekTitle.className = 'peek-title';
peekPanel.appendChild(peekTitle);
document.body.appendChild(peekPanel);
let peekHome = null;   // { body, parent, next }
export const sectionPeek = createSectionPeek({
  isCollapsed: (s) => !!s && !s.hidden && s.classList.contains('collapsed'),
  // A focused checkbox or empty field must not pin the panel.
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
    // A peek counts as the assistant's first expand.
    if (s.id === ASSISTANT_SECTION) sections.runHook(s.id, false);
    peekHome = { body, parent: body.parentNode, next: body.nextSibling };
    peekTitle.textContent = head.querySelector('.dlbl')?.textContent || '';
    peekPanel.appendChild(body);
    peekPanel.hidden = false;
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
  // Only the Alt key-press route defers to a focused text control.
  if (head) { sectionPeek.enterHead(head.closest('.fsection'), e.altKey); return; }
  if (e.target.closest?.('#section-peek')) sectionPeek.enterPeek();
});
document.addEventListener('mouseout', (e) => {
  const from = e.target.closest?.('.section-head, #section-peek');
  if (!from) return;
  const to = e.relatedTarget;
  if (to && to.closest && to.closest('.section-head, #section-peek') === from) return;
  sectionPeek.leave();
});
document.addEventListener('keydown', (e) => {
  if (e.key === 'Alt' && !e.repeat) {
    // While a text control has focus, Alt belongs to the typing.
    if (isTypingTarget(document.activeElement)) return;
    const head = document.querySelector('.section-head:hover');
    // preventDefault keeps a bare Alt from focusing the browser's menu bar.
    if (head) { e.preventDefault(); sectionPeek.altPressed(head.closest('.fsection')); }
  } else if (e.key === 'Escape' && sectionPeek.isOpen()) {
    e.preventDefault();
    sectionPeek.dismiss();
  }
});
// Blur too: Alt+Tab switches away without ever delivering the keyup.
document.addEventListener('keyup', (e) => { if (e.key === 'Alt') sectionPeek.altReleased(); });
window.addEventListener('blur', () => sectionPeek.altReleased());
document.addEventListener('pointerdown', (e) => {
  if (sectionPeek.isOpen() && !e.target.closest?.('#section-peek')) sectionPeek.dismiss();
}, true);
