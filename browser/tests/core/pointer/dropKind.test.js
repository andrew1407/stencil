// What a drag shows and what its drop does: the split for an image or a .stencil, one zone
// for a .json layout or a .stc script. Desktop twin: MainWindow.dropZones.gui.cpp.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dropKindOfFile, dropKindOfDrag } from '../../../js/core/pointer/dropKind.js';

test('at the drop the file name decides', () => {
  assert.equal(dropKindOfFile({ name: 'a.png', type: 'image/png' }), 'open');
  assert.equal(dropKindOfFile({ name: 'Trip.STENCIL', type: '' }), 'open');
  assert.equal(dropKindOfFile({ name: 'layout.json', type: '' }), 'layout');
  assert.equal(dropKindOfFile({ name: 'noext', type: 'application/json' }), 'layout');
  assert.equal(dropKindOfFile({ name: 'batch.stc', type: '' }), 'script');
  assert.equal(dropKindOfFile(null), 'open');
});

test('mid-drag only the MIME type is readable', () => {
  assert.equal(dropKindOfDrag(['Files'], ['application/json']), 'layout');
  assert.equal(dropKindOfDrag(['Files'], ['image/jpeg']), 'open');
  assert.equal(dropKindOfDrag(['Files'], ['']), 'open', 'a .stencil (or a .stc) has no MIME type');
  assert.equal(dropKindOfDrag(['text/uri-list', 'text/html'], []), 'open', 'an image from another page');
  assert.equal(dropKindOfDrag(null), 'open');
});
