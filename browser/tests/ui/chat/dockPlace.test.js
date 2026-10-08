// Placing the chat panel from outside its header (js/ui/chat/{geometry,dockZones}.js and the
// panel surface's openAt in panel/api.js): the float rect's top-left corner on a point inside the
// viewport, the edge zones a drag lays down, and openAt docking or floating the panel as a header
// drag would, a float forming out of the rect it is handed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

const doc = installDom();
const { floatRectAt, DOCK_SIDES, FLOAT_DEFAULT } = await import('../../../js/ui/chat/geometry.js');
const { createDockZones } = await import('../../../js/ui/chat/dockZones.js');
const { createPanelApi } = await import('../../../js/ui/chat/panel/api.js');

test('floatRectAt puts the rect\'s top-left corner on the point, its size kept', () => {
  assert.deepEqual(floatRectAt({ x: 0, y: 0, w: 360, h: 440 }, 640, 300, 1280, 800), { x: 640, y: 300, w: 360, h: 440 });
  assert.deepEqual(floatRectAt(null, 40, 30, 1280, 800), { x: 40, y: 30, w: FLOAT_DEFAULT.w, h: FLOAT_DEFAULT.h });
});

test('floatRectAt shifts the rect left or up to stay inside the viewport near an edge', () => {
  assert.deepEqual(floatRectAt({ w: 360, h: 440 }, 1100, 200, 1280, 800), { x: 920, y: 200, w: 360, h: 440 });
  assert.deepEqual(floatRectAt({ w: 360, h: 440 }, 200, 700, 1280, 800), { x: 200, y: 360, w: 360, h: 440 });
  assert.deepEqual(floatRectAt({ w: 360, h: 440 }, 1279, 799, 1280, 800), { x: 920, y: 360, w: 360, h: 440 });
});

test('the zones: one band per side on <body>, one lit at a time, gone on hide', () => {
  const zones = createDockZones();
  zones.show();
  zones.show();
  assert.equal(doc.body.children.length, 1, 'a second show lays nothing more');
  const layer = doc.body.children[0];
  assert.equal(layer.className, 'chat-dock-zones');
  assert.deepEqual(layer.children.map((z) => z.dataset.side), [...DOCK_SIDES]);
  zones.highlight('top');
  assert.deepEqual(layer.children.filter((z) => z.classList.contains('chat-dock-zone-active')).map((z) => z.dataset.side), ['top']);
  zones.highlight(null);
  assert.ok(!layer.children.some((z) => z.classList.contains('chat-dock-zone-active')));
  zones.hide();
  assert.equal(doc.body.children.length, 0);
  assert.equal(zones.shown, false);
});

const apiRig = (open = false) => {
  const log = [];
  let isOpen = open;
  const api = createPanelApi({
    app: {}, ctrl: () => null, turn: {}, renderAttachments() {}, updateControls() {}, voice: () => null,
    setOpen: (on) => { log.push(['setOpen', on]); isOpen = on; },
    panelIsOpen: () => isOpen,
    adoptLayout: () => log.push('adopt'),
    setDock: (m) => log.push(['setDock', m]),
    floatAt: (x, y) => log.push(['floatAt', x, y]),
    openFrom: (from, run) => { log.push(['from', from]); run(); log.push(['from', null]); },
  });
  return { api, log };
};

test('openAt a side docks there and opens, as the user\'s own layout', () => {
  const { api, log } = apiRig();
  api.openAt('Left');
  assert.deepEqual(log, ['adopt', ['from', null], ['setDock', 'left'], ['setOpen', true], ['from', null]]);
});

test('openAt a point floats the panel there, forming out of the rect it is handed', () => {
  const { api, log } = apiRig();
  const from = { left: 688, top: 408, width: 24, height: 24 };
  api.openAt({ x: 700, y: 420 }, { from });
  assert.deepEqual(log, ['adopt', ['floatAt', 700, 420], ['from', from], ['setDock', 'float'], ['setOpen', true],
    ['from', null]], 'the origin lasts for this entrance only, so the close goes home to the icon');
});

test('openAt on an open panel moves it without opening it again', () => {
  const { api, log } = apiRig(true);
  api.openAt('bottom');
  assert.deepEqual(log, ['adopt', ['from', null], ['setDock', 'bottom'], ['from', null]]);
});

test('openAt refuses what is neither a side nor a point', () => {
  const { api, log } = apiRig();
  assert.throws(() => api.openAt('float'), /Unknown dock side/);
  assert.throws(() => api.openAt({ x: 1 }), /side or a client point/);
  assert.deepEqual(log, []);
});
