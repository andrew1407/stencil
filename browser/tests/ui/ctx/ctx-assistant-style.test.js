// Styling the menu section (components.css): the assistant block sizes with the menu in
// both themes, and the flyout composer resizes on the panel's own slider handle.
import { test } from 'node:test';
import assert from 'node:assert';
import { layout } from '../../../js/ui/layout.js';
import { assistantItemHtml } from '../../../js/ui/contextMenu/contextMenu.js';
import { COMPONENTS_CSS } from '../../helpers/css.js';
import { wireContextMenu } from '../../helpers/ctxMenuChatRig.js';
import { wireBothSurfaces } from '../../helpers/chatSurfacesRig.js';

// ── Styling: the section must theme with the menu and fit its width ──
test('components.css sizes the assistant section for the menu in both themes', () => {
  const css = COMPONENTS_CSS;
  const block = css.slice(css.indexOf('.ctx-assist {'), css.indexOf('/* Hotkey hint shown'));
  assert.ok(block.includes('user-select: text'), 'chat text is selectable inside the user-select:none menu');
  assert.ok(/max-width: calc\(100vw - 40px\)/.test(block), 'never wider than the viewport');
  // The flyout is a tall chat WINDOW and the transcript flexes to fill it — a bare
  // min-height left the transcript sitting at its floor in a short box.
  assert.ok(block.includes('height: min(72vh, 600px)'), 'the chat window itself is tall, scaled to the viewport');
  assert.ok(/\.ctx-assist \{[^}]*display: flex;[^}]*flex-direction: column;/s.test(block), 'column layout drives the fill');
  assert.ok(block.includes('flex: 1 1 0'), 'the transcript takes every pixel the composer leaves');
  assert.ok(block.includes('min-height: 300px'), 'never shorter than 300px');
  assert.ok(block.includes('max-height: min(60vh, 520px)') && block.includes('overflow-y: auto'),
    'never taller than ~60vh, then scrolls in place — the flyout stays on-screen');
  assert.ok(block.includes('overscroll-behavior: contain'), 'transcript scrolling never chains to the page');
  // Theme vars only — no hardcoded colours (light/dark both follow the menu).
  assert.ok(/var\(--input-bg\)/.test(block) && /var\(--accent\)/.test(block) && /var\(--border-main\)/.test(block));
  assert.ok(!/#[0-9a-f]{6}/i.test(block), 'no hardcoded hex colours');
});

test('the flyout composer is resizable with the panel\'s slider handle', async () => {
  const html = assistantItemHtml();
  // The strip sits between the attachments row and the composer, as in the panel.
  const iAttach = html.indexOf('id="ctx-assist-attachments"');
  const iSizer = html.indexOf('id="ctx-assist-sizer"');
  const iRow = html.indexOf('id="ctx-assist-input"');
  assert.ok(iAttach < iSizer && iSizer < iRow, 'sizer strip above the input, below the chips');
  // No tooltip: a grab handle explains itself, and the panel's carries none either.
  assert.ok(!/id="ctx-assist-sizer"[^>]*title=/.test(html), 'the sizer needs no tooltip');
  // Wired with the SAME helper as the panel; a drag re-places the FLYOUT only, and marks it engaged.
  const m = await wireContextMenu();
  let measured = 0;
  Object.defineProperty(m.menu, 'offsetWidth', { get() { measured += 1; return 120; } });
  m.openAt();
  const at = [m.menu.style.left, m.menu.style.top];
  m.flyout.item.fire('click', { target: m.flyout.item });
  assert.ok(m.flyout.el.classList.contains('ctx-sub-visible'), 'the flyout is open');
  const sizer = m.doc.getElementById('ctx-assist-sizer');
  m.flyout.el.style.left = 'stale';
  sizer.fire('pointerdown', { clientY: 300 });
  assert.strictEqual(m.flyout.el._keepOpen(), true, 'a moving flyout is never "left"');
  window.fire('pointermove', { clientY: 260 });
  assert.notStrictEqual(m.flyout.el.style.left, 'stale', 'the flyout re-places itself as the composer grows');
  assert.strictEqual(m.flyout.input.style.height, '40px', 'the drag writes the input height');
  window.fire('pointerup');
  assert.deepStrictEqual([measured, m.menu.style.left, m.menu.style.top], [1, ...at],
    'and the ROOT menu is still placed once per open');
  const s = await wireBothSurfaces();
  s.doc.getElementById('chat-input-sizer').fire('pointerdown', { clientY: 300 });
  assert.ok(s.panel.host.classList.contains('chat-gesturing'), 'the panel uses the same helper');
  window.fire('pointermove', { clientY: 250 });
  window.fire('pointerup');
  assert.deepStrictEqual([s.panel.input.style.height, s.panel.host.classList.contains('chat-gesturing')], ['50px', false]);
  // Clamped by CSS (session-only: the drag writes an inline height, nothing persists).
  const css = COMPONENTS_CSS;
  const inputRule = css.slice(css.indexOf('#ctx-assist-input {'), css.indexOf('}', css.indexOf('#ctx-assist-input {')));
  assert.ok(inputRule.includes('resize: none') && inputRule.includes('min-height: 44px'), 'no native grip; two-row floor');
  // A px cap, deliberately: a percentage max-height resolves against the content-sized
  // composer row and silently pins the input to ~60% of the dragged height.
  assert.ok(inputRule.includes('max-height: 200px') && !inputRule.includes('max-height: 60%'), 'absolute cap');
  assert.ok(css.includes('.chat-input-sizer::before, .ctx-assist-sizer::before {'), 'shares the pill affordance');
  assert.ok(css.includes('.ctx-assist-sizer:hover::before, .ctx-assist-sizer.dragging::before'), 'and its hover/drag accent');
});
