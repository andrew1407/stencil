// The open-image question raised over another window (js/ui/modal/imageAnchor.js
// openImageConfirmAnchorsOver): over Projects it names no anchor, so it grows out of the press that raised
// it rather than out of a canvas buried under that window (user report: dust from nowhere).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from '../../helpers/dom.js';

installDom({ autoCreateById: false });
const { openImageConfirmAnchorsOver } = await import('../../../js/ui/modal/imageAnchor.js');

test('over another window the question names no anchor; on the editor it keeps the canvas and the Open control', () => {
  assert.deepEqual(openImageConfirmAnchorsOver(true), {});
  const own = openImageConfirmAnchorsOver(false);
  assert.ok('openAnchor' in own && typeof own.closeAnchor === 'function', 'the canvas in, the Open control or the canvas out');
});
