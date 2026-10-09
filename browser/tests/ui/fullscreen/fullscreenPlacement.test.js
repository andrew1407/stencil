// The fullscreen panels beside a docked chat (js/ui/fullscreen/panels.js): the strip, the
// selection overlay, the points list and its handle take the canvas's own columns, follow the
// chat as it opens, closes or docks, and ease there through the stylesheet.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installDom, createStubElement } from '../../helpers/dom.js';
import { createFsPanels, syncFsTriggers } from '../../../js/ui/fullscreen/panels.js';
import { EVENTS } from '../../../js/eventBus/appBus.js';

// A fixed box, as layoutBox reads one where no getComputedStyle answers; a chat stub is open.
const boxed = (tag, left, right, id, classes = tag === 'stencil-chat-panel' ? ['chat-open'] : []) => {
  const el = createStubElement(tag, {
    id, getBoundingClientRect: () => ({ left, right, top: 0, bottom: 800, width: right - left, height: 800 }),
  });
  el.classList.add(...classes);
  return el;
};
// The placement DOM: the fullscreen body, a window-wide viewport, and whatever chat `chat()` holds.
const placementDom = (t, chat) => {
  const handlers = {};
  const doc = installDom({ autoCreateById: true, querySelector: (sel) => (sel === 'stencil-chat-panel' ? chat() : null) },
    { window: { innerWidth: 1280, addEventListener: (channel, fn) => { handlers[channel] = fn; } } });
  t.after(() => doc.restore());
  doc.body.classList.add('fullscreen-mode');
  doc.register('canvas-viewport', boxed('div', 0, 1280, 'canvas-viewport'));
  doc.getElementById('fs-selection-panel').style.display = 'none';
  const across = (id) => { const s = doc.getElementById(id).style; return [s.left, s.width]; };
  const atRight = () => ['fs-points-panel', 'fs-panel-resizer']
    .map((id) => doc.getElementById(id).style.getPropertyValue('--fs-dock-right'));
  return { doc, handlers, across, atRight };
};

test('the strip and the selection overlay stop at a docked chat, and span the window without one', (t) => {
  let chat = null;
  const { across, atRight } = placementDom(t, () => chat);

  chat = boxed('stencil-chat-panel', 0, 340);
  syncFsTriggers();
  assert.deepEqual(across('fs-controls-panel'), ['340px', '940px'], 'a left dock pushes the strip right');
  assert.deepEqual(across('fs-selection-panel'), ['340px', '940px'], 'the overlay rides under it');
  assert.deepEqual(atRight(), ['', ''], 'the list keeps the right edge, nothing is docked there');
  chat = boxed('stencil-chat-panel', 940, 1280);
  syncFsTriggers();
  assert.deepEqual(across('fs-controls-panel'), ['0px', '940px'], 'a right dock stops it short');
  assert.deepEqual(atRight(), ['340px', '340px'], 'the list and its handle shift in by the chat');
  chat = null;
  syncFsTriggers();
  assert.deepEqual(across('fs-controls-panel'), ['0px', '1280px'], 'alone, the window\'s own columns, in px so they can ease');
  assert.deepEqual(across('fs-selection-panel'), ['0px', '1280px']);
  assert.deepEqual(atRight(), ['', '']);
});

test('a chat on its way out, or one docked outside fullscreen, places nothing', (t) => {
  let chat = boxed('stencil-chat-panel', 0, 340, '', ['chat-open', 'chat-closing']);
  const { doc, across } = placementDom(t, () => chat);
  syncFsTriggers();
  assert.deepEqual(across('fs-controls-panel'), ['0px', '1280px'], 'the strip takes the room as the chat dissolves');
  chat = boxed('stencil-chat-panel', 0, 340);
  doc.body.classList.remove('fullscreen-mode');
  syncFsTriggers();
  assert.deepEqual(across('fs-controls-panel'), ['', ''], 'the page\'s own columns are never written');
});

test('the panels follow the chat\'s layout and the fullscreen switch over the app bus', (t) => {
  let chat = null;
  const { doc, handlers, across } = placementDom(t, () => chat);
  createFsPanels({
    fsControlsPanel: doc.getElementById('fs-controls-panel'), fsPointsPanel: doc.getElementById('fs-points-panel'),
    showPoints: () => {}, dust: () => false,
  });
  chat = boxed('stencil-chat-panel', 0, 340);
  handlers[EVENTS.chatLayoutChanged]();
  assert.deepEqual(across('fs-controls-panel'), ['340px', '940px'], 'a dock change places the strip at once');
  chat = null;
  handlers[EVENTS.fullscreenChanged]();
  assert.deepEqual(across('fs-controls-panel'), ['0px', '1280px'], 'the switch re-reads the columns');
});

test('the stylesheet eases the strip, the overlay, the list and its handle to their new columns', () => {
  const css = readFileSync(new URL('../../../css/components/fullscreen.css', import.meta.url), 'utf8');
  assert.match(css, /#fs-controls-panel \{[^}]*transition: transform 0\.25s ease, opacity 0\.25s ease, left 0\.25s ease, width 0\.25s ease;/);
  assert.match(css, /#fs-points-panel \{[^}]*transition: transform 0\.25s ease, opacity 0\.25s ease, right 0\.25s ease;/);
  assert.match(css, /#fs-panel-resizer \{[^}]*transition: transform 0\.25s ease, opacity 0\.25s ease, right 0\.25s ease;/);
  assert.match(css, /#fs-selection-panel \{[^}]*transition: top 0\.25s ease, left 0\.25s ease, width 0\.25s ease;/);
  // Under the dust the slide is the motes', but the follow still eases.
  assert.match(css, /#fs-controls-panel\.dust-driven \{ transition: left 0\.25s ease, width 0\.25s ease; \}/);
  assert.match(css, /#fs-points-panel\.dust-driven \{ transition: right 0\.25s ease; \}/);
});

test('the stylesheet shifts the list and its handle by --fs-dock-right, the handle past the list\'s own width', () => {
  const css = readFileSync(new URL('../../../css/components/fullscreen.css', import.meta.url), 'utf8');
  assert.match(css, /#fs-points-panel \{[^}]*right: var\(--fs-dock-right, 0px\);/);
  assert.match(css, /#fs-panel-resizer \{[^}]*right: calc\(clamp\(240px, var\(--coord-panel-width, var\(--coord-panel-default\)\), 86vw\) \+ var\(--fs-dock-right, 0px\)\);/);
});
