// Every menu, flyout and dialog that plays the dust, and the control each one aims it at —
// plus the hidden/unhidden ordering around a flight, which is the whole contract for the last two.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import { assistantSrc } from './helpers/sources.js';

const css = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

// Every menu/dialog plays the dust, and every one of them still aims it at the control it grew from.
test('every icon-anchored surface dusts from — and back into — its own control', () => {
  const cases = [
    // [file, what opens it, the point it is aimed at]
    ['../src/lib/actionMenu.js', 'surfaceIn(menuEl, openOrigin);', 'surfaceOut(menuEl, openOrigin);'],
    ['../src/lib/actionMenu.js', 'surfaceIn(fly, centerOf(head));', 'surfaceOut(fly, centerOf(head));'],
    // customSelect hands both halves to showMenu/hideMenu, which aim at the trigger too.
    ['../src/lib/dropdownMenu.js', 'surfaceIn(menu, menuDustPoint(trigger), { ms: MENU_IN_MS })',
                                   'surfaceOut(menu, menu.hidden ? null : menuDustPoint(menu.__ddTrigger), { ms: MENU_OUT_MS })'],
    ['../src/lib/chatMsgMenu.js', 'surfaceIn(el, openOrigin);', 'surfaceOut(el, openOrigin);'],
    ['../src/popup/dialogShell.js', 'surfaceIn(box, origin', 'surfaceOut(box, origin);'],
    ['../src/options/confirmDialog.js', 'surfaceIn(box, origin);', 'surfaceOut(box, origin);'],
    // The Main-theme picker drives its own open/close but borrows the SAME caret point showMenu aims
    // at: its trigger is a full-width field, so the centre put the motes in the middle of the label.
    ['../src/options/appearance.js', 'surfaceIn(menu, menuDustPoint(trigger));', 'surfaceOut(menu, menuDustPoint(trigger));'],
    // The chat composer's "…" — the last one still hard-cutting on both edges.
    ['../src/popup/assistant/composerMenu.js', 'surfaceIn(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_IN_MS })',
                                  'surfaceOut(moreMenu, centerOf(moreBtn), { ms: SURFACE_MENU_OUT_MS })'],
  ];
  for (const [rel, opens, closes] of cases) {
    const s = css(rel);
    assert.ok(s.includes(opens), `${rel} forms from ${opens}`);
    assert.ok(s.includes(closes), `${rel} comes apart into ${closes}`);
  }
  // The logo's drag menu names the MARK as its point, not the menu's top-left corner.
  assert.match(css('../src/lib/logoDragMenu.js'),
    /placeMenu\(r\.left, r\.bottom \+ 6, \{ x: r\.left \+ r\.width \/ 2, y: r\.top \+ r\.height \/ 2 \}\)/);
});

// `hidden` is display:none, so there is nothing left to copy once it is set — the order
// around each flight is the whole contract for this one.
test('the composer "…" is unhidden before it forms, and hidden right after it leaves', () => {
  const src = assistantSrc();
  const s = src.slice(src.indexOf('const setMoreOpen = (on) =>'));
  assert.ok(s.indexOf('if (on) moreMenu.hidden = false;') < s.indexOf('surfaceIn(moreMenu'));
  assert.ok(s.indexOf('surfaceOut(moreMenu') < s.indexOf('if (!on) moreMenu.hidden = true;'));
  // Re-closing a closed menu (Escape with nothing up) must not raise a second cloud.
  assert.match(s, /if \(on === !moreMenu\.hidden\) return;/);
  // Every close path still goes through it.
  assert.match(src, /const closeMore = \(\) => setMoreOpen\(false\);/);
});

test('a close is SYNCHRONOUS: the motes are the surface leaving, nothing waits on them', () => {
  const menu = readFileSync(new URL('../src/lib/actionMenu.js', import.meta.url), 'utf8');
  const close = menu.slice(menu.indexOf('const close = () =>'));
  // Dusted while it is still on screen and measurable, hidden on the very same frame —
  // which is what lets a burst of open/close land on the true state.
  assert.ok(close.indexOf('surfaceOut(menuEl, openOrigin);') < close.indexOf('menuEl.hidden = true;'));
  const dlg = readFileSync(new URL('../src/popup/dialogShell.js', import.meta.url), 'utf8');
  assert.ok(dlg.indexOf('surfaceOut(box, origin);') < dlg.indexOf('back.remove();'));
});
