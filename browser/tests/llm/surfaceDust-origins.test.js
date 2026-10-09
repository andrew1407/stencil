// Each surface's dust point (js/ui/motion.js surfaceIn / surfaceOut): modals and the confirm
// dialog, the chat panel and the ⋯/… overflow menus, driven through their own open and close;
// the context menu's is ui/contextMenu/ctx-dust.test.js. Split from surfaceDust-surfaces.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { createStubElement } from '../helpers/dom.js';
import { installDustDom, rect, boxEl, cloudAim, near, cloudKind } from '../helpers/dustRig.js';

const dust = installDustDom({
  docOpts: { autoCreateById: true },
  globals: { window: { innerWidth: 1200, innerHeight: 800, addEventListener() {}, removeEventListener() {}, dispatchEvent() {} } },
});
const { doc } = dust;
// A built node measures only while it is in the document, as a real one does.
const ZERO = rect(0, 0, 0, 0);
doc.createElement = (tag) => {
  const el = createStubElement(tag, { focus() {}, select() {}, getBoundingClientRect: () => (el.parentNode ? rect(0, 0, 160, 90) : ZERO) });
  return el;
};

const { SURFACE_OUT_MS, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS, dockAwayPoint } = await import('../../js/ui/motion.js');
const { setMotionPrefs } = await import('../../js/ui/motion/motionPrefs.js');
const { wireModalShell, MODAL_CLOSE_MS } = await import('../../js/ui/base.js');
const { StencilConfirmModal } = await import('../../js/ui/modal/confirmModal.js');
const { createProjectRowMenu } = await import('../../js/ui/projects/window/projectRowMenu.js');
const { wireChatRowMenu, wireChatMoreMenu, visibleChatMoreBtn } = await import('../../js/ui/chat/view.js');
const { wireOpenState } = await import('../../js/ui/chat/panel/openState.js');

const ev = (extra = {}) => ({ preventDefault() {}, stopPropagation() {}, ...extra });

const modalBox = () => {
  const box = boxEl(rect(300, 200, 400, 300));
  const overlay = createStubElement('div', { querySelector: (sel) => (sel === '.app-modal' ? box : null) });
  return { box, overlay };
};

test('modals: the dust point IS the icon centre setOriginVars already measured', () => {
  setMotionPrefs({ mode: 'particles' });
  const { box, overlay } = modalBox();
  const opener = boxEl(rect(20, 10, 30, 30));
  const shell = wireModalShell(overlay, null, null, { originEl: () => opener });
  shell.open();
  // The very same cx/cy that feed --modal-dx/dy: an on-screen opener's centre.
  assert.ok(near(cloudAim(box.__dustHost), { x: 35, y: 25 }), 'the window forms out of its opener');
  assert.equal(cloudKind(box), 'dust-forming');
  assert.equal(box.style['--modal-dx'], `${35 - 500}px`);
  shell.close();
  assert.ok(overlay.classes.has('modal-closing'), 'the close plays under modal-closing…');
  assert.equal(cloudKind(box), 'dust-leaving', '…measured while still open');
  assert.ok(near(cloudAim(box.__dustHost), { x: 35, y: 25 }), 'and pours back into the opener');
  shell.openPopover(opener);
  assert.ok(overlay.classes.has('modal-popover') && cloudKind(box) === 'dust-forming', 'the popover shape plays it too');
  shell.close();
  shell.open(null);
  assert.ok(near(cloudAim(box.__dustHost), { x: 500, y: -90 }), 'no control on screen: falls from above the box');
  // The window still goes away on its own clock — the close never waits on the effect.
  assert.equal(MODAL_CLOSE_MS, SURFACE_OUT_MS);
  setMotionPrefs({ mode: 'none' });
  shell.close();
  assert.equal(box.__dustHost, null, 'nothing plays: what the open left in the air is dropped');
  setMotionPrefs({ mode: 'particles' });
});

// The CONFIRM dialog has no opener icon at all: it plays the very same flight out of the
// gesture that raised it (ui/canvas/gesturePoint.js), unless the caller named one (ui/modal/imageAnchor.js).
test('the confirm dialog forms out of its gesture and returns to the anchor it opened with', async () => {
  const { box, overlay } = modalBox();
  doc.register('confirm-modal-overlay', overlay);
  const modal = new StencilConfirmModal();
  modal.wire();
  doc.dispatch('pointerdown', { clientX: 100, clientY: 120 });
  const plain = modal.ask('Sure?');
  assert.ok(near(cloudAim(box.__dustHost), { x: 100, y: 120 }), 'out of the press that raised it');
  // A press on the dismiss button is not where it goes back to.
  doc.dispatch('pointerdown', { clientX: 900, clientY: 700 });
  doc.getElementById('confirm-modal-cancel').dispatch('click');
  assert.equal(await plain, false);
  assert.equal(cloudKind(box), 'dust-leaving');
  assert.ok(near(cloudAim(box.__dustHost), { x: 100, y: 120 }), 'the close reuses the open anchor');
  const named = modal.ask('Open?', { openAnchor: rect(600, 400, 40, 20),
                                     closeAnchor: (val) => (val ? rect(10, 700, 20, 20) : null) });
  assert.ok(near(cloudAim(box.__dustHost), { x: 620, y: 410 }), 'a named anchor wins over the gesture');
  doc.getElementById('confirm-modal-confirm').dispatch('click');
  assert.equal(await named, true);
  assert.ok(near(cloudAim(box.__dustHost), { x: 20, y: 710 }), 'the close anchor may be a function of the answer');
});

// The chat panel's open state (ui/chat/panel/openState.js) on a host that measures only while open.
const chatPanel = (openBtn = null) => {
  const host = doc.register('chat-panel', createStubElement('div', {
    querySelectorAll: () => [],
    getBoundingClientRect: () => (host.classes.has('chat-open') ? rect(0, 0, 360, 800) : ZERO),
  }));
  const state = wireOpenState({ host, input: createStubElement('textarea'), openBtn, resizer: null, header: null,
    backdrop: null, closeBtn: createStubElement('button'), panelIsOpen: () => host.classes.has('chat-open'), refreshStatus() {}, invalidatePillRects() {} });
  return { host, ...state };
};

test('the chat panel keeps its dock edge, and a float keeps its icon', () => {
  const { host, setOpen } = chatPanel();
  setOpen(true);
  // Opened AFTER the class, or the panel is display:none and measures nothing.
  assert.equal(cloudKind(host), 'dust-forming');
  assert.equal(host.style['--dust-ms'], '630ms');
  assert.ok(near(cloudAim(host.__dustHost), dockAwayPoint(rect(0, 0, 360, 800), 'left')), 'a dock streams in past its edge');
  // Closed BEFORE it leaves the screen, and on the same clock the class swap uses.
  setOpen(false);
  assert.equal(cloudKind(host), 'dust-leaving');
  assert.ok(host.classes.has('chat-closing') && host.classes.has('chat-open'), 'still up while it pours out');
  assert.equal(host.style['--dust-ms'], '510ms');
  const icon = boxEl(rect(640, 8, 28, 28));
  const float = chatPanel(icon);
  float.setDock('float');
  float.setOpen(true);
  assert.ok(near(cloudAim(float.host.__dustHost), { x: 654, y: 22 }), 'a float flies out of the toolbar icon, like a modal');
});

test('the ⋯ overflow menus grow out of the button (or the right-click) that opened them', () => {
  // Projects: the cursor for a right-click, else the "⋯" centre — and back into it; the node
  // still goes NOW (a detached node measures nothing), the layer owns its own lifetime.
  const rows = createProjectRowMenu();
  const dots = boxEl(rect(500, 100, 24, 24));
  rows.showMenu(dots, [{ icon: 'x', label: 'Remove', onClick() {} }]);
  let menu = doc.body.children.find((c) => c.classes?.has('project-menu'));
  assert.ok(near(cloudAim(menu.__dustHost), { x: 512, y: 112 }), 'out of the ⋯ centre');
  assert.equal(menu.style['--dust-ms'], `${SURFACE_MENU_IN_MS}ms`);
  rows.showMenu(dots, [{ icon: 'x', label: 'Remove', onClick() {} }], { x: 300, y: 40 });
  assert.equal(cloudKind(menu), 'dust-leaving', 'the old menu leaves as dust, measured before it went');
  assert.equal(menu.parentNode, null);
  assert.equal(menu.style['--dust-ms'], `${SURFACE_MENU_OUT_MS}ms`);
  menu = doc.body.children.find((c) => c.classes?.has('project-menu'));
  assert.ok(near(cloudAim(menu.__dustHost), { x: 300, y: 40 }), 'a right-click grows out of the cursor');
  rows.closeMenu();
  assert.ok(near(cloudAim(menu.__dustHost), { x: 300, y: 40 }), '…and pours back into it');
  // The chat bubble's "⋯" is cursor-anchored, same open point both ways.
  const transcript = createStubElement('div', { contains: () => true });
  const rowEl = createStubElement('div', { closest: (s) => (s === '.chat-msg' ? rowEl : null), _chatRow: { role: 'assistant', text: 'hi' } });
  wireChatRowMenu(transcript, {});
  transcript.dispatch('contextmenu', ev({ target: rowEl, clientX: 40, clientY: 60 }));
  const chatMenu = doc.body.children.find((c) => c.classes?.has('chat-row-menu'));
  assert.ok(near(cloudAim(chatMenu.__dustHost), { x: 40, y: 60 }));
  assert.equal(chatMenu.style['--dust-ms'], `${SURFACE_MENU_IN_MS}ms`);
  transcript.dispatch('contextmenu', ev({ target: rowEl, clientX: 400, clientY: 300 }));
  assert.equal(cloudKind(chatMenu), 'dust-leaving');
  assert.equal(chatMenu.parentNode, null, 'the node goes at once');
  assert.ok(near(cloudAim(chatMenu.__dustHost), { x: 40, y: 60 }), 'back into the point it grew out of');
});

// A composer "…" and its menu: the menu measures only while shown (`hidden` is display:none).
const moreMenu = (prefix, btnBox) => {
  const btn = doc.register(`${prefix}-more-btn`, boxEl(btnBox));
  const item = createStubElement('button');
  const menu = doc.register(`${prefix}-more-menu`, createStubElement('span', {
    getBoundingClientRect: () => (menu.hidden ? ZERO : rect(btnBox.left - 150, btnBox.top - 200, 180, 190)),
    querySelectorAll: () => [item],
  }));
  menu.hidden = true;
  wireChatMoreMenu(prefix, doc);
  return { btn, menu, item };
};

test('the composer "…" menu forms out of, and pours back into, its own trigger', () => {
  const { btn, menu, item } = moreMenu('chat', rect(700, 600, 30, 30));
  btn.dispatch('click', ev());
  assert.equal(menu.hidden, false, 'unhidden before it forms');
  assert.equal(cloudKind(menu), 'dust-forming');
  assert.ok(near(cloudAim(menu.__dustHost), { x: 715, y: 615 }), 'out of the trigger centre');
  assert.equal(menu.style['--dust-ms'], `${SURFACE_MENU_IN_MS}ms`);
  btn.dispatch('click', ev());
  assert.equal(cloudKind(menu), 'dust-leaving', 'measured while still up');
  assert.equal(menu.hidden, true, 'hidden straight after');
  assert.ok(near(cloudAim(menu.__dustHost), { x: 715, y: 615 }));
  // Only a real change flies: re-closing a closed menu raises no second cloud.
  const layers = dust.clouds().length;
  item.dispatch('click', ev());
  assert.equal(dust.clouds().length, layers);
});

// The gear that raises the settings window sits INSIDE that menu, which closes as it is clicked,
// so the flight is anchored on the "…" — measuring the gear by then gives 0x0.
test('the assistant settings window flies to the "…", not to the gear that vanished', async () => {
  const panel = moreMenu('chat', rect(700, 600, 30, 30));
  const flyout = moreMenu('ctx-assist', rect(200, 300, 30, 30));
  // The composer the user last opened wins, then whichever surface is actually up.
  flyout.btn.dispatch('click', ev());
  assert.equal(visibleChatMoreBtn(doc), flyout.btn);
  flyout.btn.getBoundingClientRect = () => ZERO;
  assert.equal(visibleChatMoreBtn(doc), panel.btn, 'a hidden trigger is never offered');
  panel.btn.getBoundingClientRect = () => ZERO;
  assert.equal(visibleChatMoreBtn(doc), null, 'null is the honest answer: the shell falls from above');
  // The shell asks for its origin when it OPENS, and close() re-measures that control.
  const { box, overlay } = modalBox();
  let asked = 0;
  const shell = wireModalShell(overlay, null, null, { originEl: () => { asked++; return visibleChatMoreBtn(doc); } });
  assert.equal(asked, 0, 'not resolved at wiring time');
  flyout.btn.getBoundingClientRect = () => rect(200, 300, 30, 30);
  shell.open();
  assert.equal(asked, 1);
  assert.ok(near(cloudAim(box.__dustHost), { x: 215, y: 315 }));
  flyout.btn.getBoundingClientRect = () => rect(260, 340, 30, 30);
  shell.close();
  assert.ok(near(cloudAim(box.__dustHost), { x: 275, y: 355 }), 'the close lands where the "…" is now');
  // …and the assistant settings window is one such shell, anchored on the "…".
  const llm = modalBox();
  doc.register('chat-settings-overlay', llm.overlay);
  const { StencilLlmSettingsModal } = await import('../../js/ui/llmSettings/modal.js');
  StencilLlmSettingsModal.prototype.wire.call(llm.overlay, {});
  llm.overlay.__stencilModal.open();
  assert.ok(near(cloudAim(llm.box.__dustHost), { x: 275, y: 355 }), 'the settings window grows out of the "…"');
});

