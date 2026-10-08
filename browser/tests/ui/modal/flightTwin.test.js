// In fullscreen the toolbar the user sees is the strip's clone (ui/fullscreen/clones.js), which keeps the
// hidden original's id and relays its click: a window opened from it must grow out of the clone, not fall
// from above the screen while the clone is in plain sight.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const rect = (left, top, width, height) => () => ({ left, top, width, height, right: left + width, bottom: top + height });
const HIDDEN = rect(0, 0, 0, 0);

test('shownRect: a hidden control answers with its shown same-id clone', async () => {
  const original = createStubElement('button', { id: 'projects-btn', getBoundingClientRect: HIDDEN });
  const clone = createStubElement('button', { id: 'projects-btn', getBoundingClientRect: rect(300, 8, 28, 24) });
  const doc = installDom({ querySelectorAll: (sel) => (sel === '[id="projects-btn"]' ? [original, clone] : []) },
                         { window: { innerWidth: 1200, innerHeight: 800 } });
  try {
    const { shownRect } = await import('../../../js/ui/modal/flight.js');
    assert.deepEqual(shownRect(original), clone.getBoundingClientRect());
    assert.deepEqual(shownRect(clone), clone.getBoundingClientRect());
    clone.getBoundingClientRect = HIDDEN;
    assert.equal(shownRect(original), null, 'nothing shown: the flight falls from above');
    assert.equal(shownRect(createStubElement('button', { getBoundingClientRect: HIDDEN })), null);
  } finally {
    doc.restore();
  }
});
