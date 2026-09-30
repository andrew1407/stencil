// A project row's "Make a copy ›" entry (ui/projects/row/copyItem.js): each scope opens the copy
// confirmation for that row, and the list follows the copy once it lands.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../../helpers/dom.js';
import { copyMenuItem, afterRowCopy } from '../../../../js/ui/projects/row/copyItem.js';

test('each scope opens the confirmation for the row, flying back into its live "⋯"', () => {
  const doc = installDom();
  const opened = [];
  doc.querySelector = (sel) => (sel === 'stencil-copy-project-modal'
    ? { openFor: (target, anchors) => opened.push([target, anchors]) } : null);
  const anchor = createStubElement('button', { isConnected: true });
  const onDone = () => {};
  const item = copyMenuItem({ target: { id: 'p1' }, anchor, onDone });
  assert.deepEqual(item.items.map((i) => i.label), ['Image only', 'Image and layout', 'Whole project']);
  item.items[2].onClick('rect');
  const [[target, { from, backTo }]] = opened;
  assert.deepEqual(target, { id: 'p1', what: 'project', onDone });
  assert.equal(from, 'rect');
  assert.equal(backTo(), anchor, 'a still-mounted "⋯" is the one it closes into');
});

test('after a copy: an open here leaves the window; anything else re-lists and shows the new row', () => {
  const calls = [];
  const follow = afterRowCopy({
    close: () => calls.push('close'), render: () => calls.push('render'),
    invalidateRemotes: () => calls.push('invalidate'), scrollRowIntoView: (id) => calls.push(['scroll', id]),
  });
  follow('n1', 'here');
  assert.deepEqual(calls.splice(0), ['close']);
  follow('n1', 'none');
  assert.deepEqual(calls.splice(0), ['invalidate', 'render', ['scroll', 'n1']]);
  follow(null, 'here');
  follow(null, 'newtab');
  assert.deepEqual(calls, ['close', 'invalidate', 'render'], 'an unsaved incognito copy has no row to show');
});
