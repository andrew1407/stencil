// The chat float's flight (js/ui/chat/panel/openState.js) on a wired panel: dropped open by its
// toolbar icon it forms out of the cursor, and its close still flies home to the icon; a plain
// open keeps flying from the icon.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { wireBothSurfaces } from '../../../helpers/chatSurfacesRig.js';
import { dropAnchor } from '../../../../js/ui/drag/iconDrag.js';

const ICON = { left: 600, top: 20, right: 628, bottom: 48, width: 28, height: 28 };
const flight = (host) => [host.style['--modal-dx'], host.style['--modal-dy']];

test('a float dropped open forms out of the cursor, and its close flies home to the icon', async () => {
  const s = await wireBothSurfaces();
  s.doc.getElementById('chat-btn').rect = ICON;
  const host = s.panel.host;
  s.app.chat.openAt({ x: 300, y: 200 }, { from: dropAnchor(300, 200) });
  assert.ok(host.classList.contains('chat-dock-float') && host.classList.contains('chat-open'));
  assert.equal(host.style.left, '300px', 'its top-left corner on the release point');
  assert.equal(host.style.top, '200px');
  // The float is 360 × 440 at (300, 200), so its centre is (480, 420).
  assert.deepEqual(flight(host), [`${300 - 480}px`, `${200 - 420}px`], 'out of the cursor');
  s.app.chat.close();
  assert.deepEqual(flight(host), [`${614 - 480}px`, `${34 - 420}px`], 'home to the icon');
});

test('a plain open of the float flies out of the icon', async () => {
  const s = await wireBothSurfaces();
  s.doc.getElementById('chat-btn').rect = ICON;
  s.app.chat.dock('float');
  s.app.chat.open();
  // The default float sits at (80, 80), 360 × 440: its centre is (260, 300).
  assert.deepEqual(flight(s.panel.host), [`${614 - 260}px`, `${34 - 300}px`]);
});
