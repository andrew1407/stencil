// The context menu's "Assistant ▸" entry (js/ui/contextMenu.js): a submenu parent whose
// flyout is a compact chat, gated on the configured provider and built by syncAssistant.
import { test } from 'node:test';
import assert from 'node:assert';
import { assistantEnabled, assistantItemHtml } from '../js/ui/contextMenu.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { contextMenuSource } from './helpers/contextMenuSource.js';
import { layoutWith } from './helpers/ctxAssistantRig.js';

const ASSIST_IDS = [
  'ctx-assist-menu', 'ctx-assist-sub', 'ctx-assist', 'ctx-assist-transcript',
  'ctx-assist-attachments', 'ctx-assist-sizer', 'ctx-assist-input', 'ctx-assist-send',
  'ctx-assist-attach-btn', 'ctx-assist-attach-input', 'ctx-assist-settings-btn', 'ctx-assist-status-dot',
];


// ── Presence / absence ──
test('the Assistant entry markup carries each id once, above Start Drawing', () => {
  const html = assistantItemHtml();
  for (const id of ASSIST_IDS) {
    assert.strictEqual(html.split(`id="${id}"`).length - 1, 1, `id="${id}" appears exactly once`);
  }
  // Same placeholder wording as the panel, and Enter/Shift+Enter documented in it.
  assert.ok(html.includes('placeholder="Ask the assistant… (Enter sends, Shift+Enter newline)"'));
  // Built by syncAssistant directly ABOVE the script entry, itself above Start Drawing.
  const src = contextMenuSource();
  assert.ok(src.includes("getElementById('ctx-script') || document.getElementById('ctx-draw-toggle')"),
            'anchored to the script entry, falling back to Start Drawing');
  assert.ok(src.includes("anchor.insertAdjacentHTML('beforebegin', assistantItemHtml());"), 'inserted immediately above it');
  // Send ships disabled (nothing typed yet) and becomes Stop at runtime.
  assert.ok(html.includes('id="ctx-assist-send" disabled'));
});

// The chat must be a SUBMENU and the root menu must never be re-clamped while it is open:
// moving it under a stationary cursor fires mouseleave and kills the hovered item's flyout.
test('the Assistant entry is a submenu PARENT, structured like Style / Image Filter', () => {
  const html = assistantItemHtml();
  // A .ctx-item with a caret and a nested .ctx-sub — the exact shape the hover
  // wiring keys on (`:scope > .ctx-item` + `:scope > .ctx-sub`).
  assert.match(html, /<div class="ctx-item" id="ctx-assist-menu">/);
  assert.ok(html.includes('class="ctx-arrow"'), 'carries the ▸ caret like the other parents');
  assert.match(html, /<div class="ctx-sub ctx-assist-sub" id="ctx-assist-sub">/);
  assert.ok(html.indexOf('ctx-arrow') < html.indexOf('ctx-assist-sub'), 'caret precedes the flyout, as in the others');
  // The chat lives INSIDE the flyout — never inline in the menu body.
  const flyout = html.slice(html.indexOf('id="ctx-assist-sub"'));
  for (const id of ['ctx-assist-transcript', 'ctx-assist-input', 'ctx-assist-send']) {
    assert.ok(flyout.includes(`id="${id}"`), `${id} lives in the flyout`);
  }
  assert.strictEqual(html.split('ctx-assist-section').length - 1, 0, 'the old inline section is gone');
  // It carries NO separator of its own — gating it off can't leave one dangling.
  assert.strictEqual(html.split('class="ctx-sep"').length - 1, 0, 'no separator ships with the entry');
});

test('the root menu is never re-positioned/observed while open (submenu-killer guard)', () => {
  const src = contextMenuSource();
  // Only the FLYOUTS may be observed: a ResizeObserver on the MENU re-clamps it as the chat
  // grows, sliding the hovered item out from under the cursor.
  assert.ok(!/ResizeObserver\([^)]*\)[\s\S]{0,80}\.observe\(menu\)/.test(src), 'no ResizeObserver on #ctx-menu');
  assert.strictEqual(src.split('.observe(sub)').length - 1, 1, 'exactly one observer, on the submenu');
  // placeMenu runs once per open, from openAt, with the click point.
  assert.strictEqual(src.split('placeMenu').length - 1, 2, 'defined once, called once');
  assert.ok(src.includes('placeMenu(x, y);'), 'called from openAt with the anchor');
  // Every submenu parent (incl. a late-built Assistant) goes through one wiring path.
  assert.ok(src.includes('const wireSubmenu = (item, sub) => {'));
  assert.ok(src.includes('if (item && sub) host.wireSubmenu(item, sub);'), 'the built-later Assistant is wired like the rest');
});

test('flyouts survive the menu moving under a stationary cursor', () => {
  const src = contextMenuSource();
  // A flyout placed while the entry pop still holds a transform anchors to the MENU (a
  // transformed ancestor is the containing block for position:fixed), so finish the pop first.
  assert.ok(src.includes('for (const a of menu.getAnimations?.() || []) a.finish();'), 'entry pop is settled before placing a flyout');
  assert.ok(src.indexOf('a.finish()') < src.indexOf("sub.style.left = '-9999px'"), 'settled BEFORE the measurement');
  const anims = ANIMATIONS_CSS;
  assert.ok(/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^}]*\}/.test(anims), 'the pop is still there…');
  assert.ok(!/#ctx-menu\.ctx-open \{[^}]*animation: menuPop[^;]*both/.test(anims), '…and still not `both`-filled');
  // 2. The pop moves items under a still cursor, which fires SYNTHETIC boundary events.
  //    Hover decisions ignore them: no pointer motion ⇒ no hover state change.
  assert.match(src, /const pointerIdle = \(\) => samePoint\(subShownPointer, lastPointer\);/);
  assert.ok(src.includes('subShownPointer = { ...lastPointer };'), 'the placement records the pointer');
  assert.ok(src.includes("for (const type of ['mousemove', 'mouseover', 'mouseout'])"),
    'pointer tracking covers the events that PRECEDE mouseenter/mouseleave');
  assert.ok(src.includes('if (pointerIdle() && activeSub && activeSub !== sub) return;'), 'no flyout stealing without motion');
  assert.ok(src.includes('if (!activeSub || keepSubOpen(activeSub) || pointerIdle()) return;'), 'no closing without motion');
  assert.ok(src.includes('positionSub(item, sub);   // the item moved, not the user — follow it'));
});

test('the menu pops out of the click point, and Escape (consumed) closes it', () => {
  const src = contextMenuSource();
  // placeMenu stamps transform-origin from the click point vs the PLACED (clamped) box.
  assert.ok(src.includes('menuPopOrigin(x, y, { left, top, width: mw, height: mh })'),
    'the origin is the click point relative to the clamped position');
  const anims = ANIMATIONS_CSS;
  assert.match(anims, /@keyframes menuPop \{ from \{ opacity: 0; transform: scale\(0\.62\); \} to \{ opacity: 1; transform: none; \} \}/,
    'the pop scales up from the origin');
  assert.match(anims, /prefers-reduced-motion: reduce\) \{\s*#ctx-menu\.ctx-open, \.chat-row-menu \{ animation: none; \}/,
    'both menus opt out under prefers-reduced-motion');
  // Escape: capture phase, consumed only while OPEN — and a chat row menu floating
  // on top goes first (its own capture closer swallows the key).
  assert.match(src,
    /if \(e\.code !== 'Escape' \|\| !menuIsOpen\(\)\) return;\s*if \(chatRowMenuOpen\(\)\) return;\s*e\.stopPropagation\(\);\s*closeMenu\(\);\s*\}, true\);/,
    'Escape closes the open menu without leaking to other app handlers');
});

test('assistantEnabled gates on the provider only', () => {
  assert.strictEqual(assistantEnabled({ provider: 'none' }), false);
  assert.strictEqual(assistantEnabled(null), false);
  assert.strictEqual(assistantEnabled(undefined), false);
  for (const p of ['ollama', 'openai-compat', 'stencil-server']) {
    assert.strictEqual(assistantEnabled({ provider: p }), true, `${p} enables the entry`);
  }
  // A configured-but-unreachable provider still gets the entry (the failure shows
  // up in the reply, like the panel) — the gate never probes the endpoint.
  const src = contextMenuSource();
  assert.ok(src.includes('const on = assistantEnabled(loadLlmSettings());'), 'syncAssistant gates on the saved settings');
});

test('the assistant entry never touches the static menu markup (built by syncAssistant)', () => {
  // The static menu is byte-identical whatever the provider — the entry is built at
  // wire()/open time, so gating it on/off can never disturb the original menu.
  const off = layoutWith({ provider: 'none' });
  const on = layoutWith({ provider: 'ollama', baseUrl: 'http://localhost:11434' });
  assert.strictEqual(on, off, 'static markup is provider-independent');
  for (const id of ASSIST_IDS) assert.ok(!off.includes(`id="${id}"`), `${id} absent from the static markup`);
  const seps = (m) => m.split('class="ctx-sep"').length - 1;
  // Anchored on the LAST item of the view group, so the slice holds only the boundary and not
  // the Image/Layout submenu's own internal separator.
  const gap = (m) => m.slice(m.indexOf('id="ctx-fullscreen"'), m.indexOf('id="ctx-draw-toggle"'));
  assert.strictEqual(seps(gap(off)), 1, 'one separator between the two groups');
  // …and the built entry carries no separator of its own, so nothing can dangle.
  assert.strictEqual(assistantItemHtml().split('class="ctx-sep"').length - 1, 0);
});
