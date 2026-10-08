// The crop window's markup (js/ui/modal/cropModal.js): the picture is sized to the window's own free
// room by cropFit.js, never by viewport units that ignore the window's size and its chrome.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { StencilCropModal } from '../../../js/ui/modal/cropModal.js';

const html = StencilCropModal.inner();
const styleOf = (re) => (re.exec(html) || [])[1] ?? '';

test('the picture fills its stage, and the stage is sized by the window, not the viewport', () => {
  const img = styleOf(/<img id="crop-image-el"[^>]*style="([^"]*)"/);
  assert.match(img, /width:100%;height:100%/, 'the image is exactly the stage the box is drawn over');
  assert.doesNotMatch(img, /v[wh]\b/, 'no viewport-unit cap: a resized window would never rescale it');
  const stage = styleOf(/<div id="crop-stage" style="([^"]*)"/);
  assert.match(stage, /flex:none/, 'the stage keeps the size cropFit gives it');
});

test('the stage sits in a frame that takes the body\'s free room, and the body never scrolls', () => {
  const frame = /<div class="crop-fit" style="([^"]*)">\s*<div id="crop-stage"/.exec(html);
  assert.ok(frame, 'the stage\'s parent is the free-room frame');
  assert.match(frame[1], /flex:1 1 auto;min-height:0;align-self:stretch/);
  const body = styleOf(/<div class="settings-body" style="([^"]*)"/);
  assert.match(body, /overflow:hidden/, 'a scrolling body is what cut the picture\'s bottom off');
  assert.match(body, /min-height:0/);
  assert.ok(html.indexOf('id="crop-dims"') < html.indexOf('class="settings-footer"'),
    'the size line stays in the body, the footer below it');
});
