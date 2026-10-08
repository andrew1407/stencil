// StencilElement's connect (js/ui/base.js): a region renders its markup into an empty host and is
// wired once the app is ready; a region connected inside a still copy of the page (COPY_ATTR, the
// theme lens) is a picture — it renders nothing, waits for nothing and is never wired.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../helpers/dom.js';

const doc = installDom();
const { StencilElement, COPY_ATTR } = await import('../../js/ui/base.js');
const { EVENTS } = await import('../../js/eventBus/appBus.js');

class Region extends StencilElement {
  static inner() { return '<p>region</p>'; }
  wire(app) { this.wiredWith = app; }
}

const region = ({ inCopy, empty = true }) => {
  const r = new Region();
  r.firstElementChild = empty ? null : {};
  r.closest = (sel) => (inCopy && sel === `[${COPY_ATTR}]` ? {} : null);
  return r;
};
const readyListeners = () => doc.listeners[EVENTS.ready]?.length ?? 0;

test('a live region renders into its empty host and is wired when the app is ready', () => {
  const before = readyListeners();
  const r = region({ inCopy: false });
  r.connectedCallback();
  assert.equal(r.innerHTML, '<p>region</p>');
  assert.equal(readyListeners(), before + 1);
  doc.dispatch(EVENTS.ready, { detail: { app: 'the app' } });
  assert.equal(r.wiredWith, 'the app');
});

test('a region inside a copy of the page is never rendered, subscribed or wired', () => {
  const before = readyListeners();
  for (const empty of [true, false]) {
    const r = region({ inCopy: true, empty });
    r.connectedCallback();
    assert.equal(r.innerHTML, undefined, 'no markup of its own');
    assert.equal(readyListeners(), before, 'no wait for a ready that would wire it');
    assert.equal(r.wiredWith, undefined);
  }
});
