// The facade's words in JavaScript: completed after `stencil.`, hovered where they stand, and
// offered in a .stcjs always but in a plain .js only once it has opted in. Every toggle is
// read per request, so turning one off needs no reload.
import test from 'node:test';
import assert from 'node:assert/strict';

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import { at, js, stcjs, withHost } from './helpers/jsHost.js';

const labels = (items) => items.map((item) => item.label);

test('a member list is offered after `stencil.`, and nothing after anything else', async () => {
  await withHost({}, ({ hints }) => {
    const items = hints.completionProvider.provideCompletionItems(stcjs('stencil.'), at(0, 8));
    assert.ok(labels(items).includes('rotateRight'));
    assert.ok(labels(items).includes('execScript'));
    assert.ok(labels(items).includes('lineColor'), 'a setting is a member too');
    assert.ok(!labels(items).includes('stencil'), 'the facade is not one of its own members');

    for (const [text, character] of [['other.', 6], ['const x = 1;', 12], ['stencil', 7]]) {
      assert.deepEqual(hints.completionProvider.provideCompletionItems(stcjs(text), at(0, character)), []);
    }
  });
});

test('a call is a function, a value is a property, and each carries its explanation', async () => {
  await withHost({}, ({ hints }) => {
    const items = hints.completionProvider.provideCompletionItems(stcjs('stencil.'), at(0, 8));
    const byName = new Map(items.map((item) => [item.label, item]));
    assert.equal(byName.get('rotateRight').kind, 2, 'Function');
    assert.equal(byName.get('zoomLevel').kind, 9, 'Property');
    assert.match(byName.get('crop').detail, /Cut the picture down\./);
    assert.match(byName.get('crop').documentation.value, /```js\nstencil\.crop\(/);
  });
});

test('a plain .js is offered nothing until it says the marker', async () => {
  await withHost({}, ({ hints }) => {
    assert.deepEqual(hints.completionProvider.provideCompletionItems(js('stencil.'), at(0, 8)), []);
    const opted = js('// @use stencil\nstencil.');
    assert.ok(labels(hints.completionProvider.provideCompletionItems(opted, at(1, 8))).length > 100);
    // Indented, and with the rest of a sentence after it, is still the marker.
    const loose = js('  // @use stencil — the browser facade\nstencil.');
    assert.ok(labels(hints.completionProvider.provideCompletionItems(loose, at(1, 8))).length > 100);
    // Where in the file it says so is its own business — see jsMarker.test.js.
  });
});

test('an empty buffer is answered, not thrown at', async () => {
  await withHost({}, ({ hints }) => {
    assert.deepEqual(hints.completionProvider.provideCompletionItems(js(''), at(0, 0)), []);
    assert.deepEqual(hints.completionProvider.provideCompletionItems(stcjs(''), at(0, 0)), []);
  });
});

test('hovering a member explains it; hovering anything else says nothing', async () => {
  await withHost({}, ({ hints }) => {
    const hover = hints.hoverProvider.provideHover(stcjs('stencil.rotateRight()'), at(0, 10));
    assert.match(hover.contents.value, /\*\*stencil\.rotateRight\*\*/);
    assert.equal(hover.contents.supportHtml, false, 'a hover renders Markdown, never HTML');
    assert.equal(hints.hoverProvider.provideHover(stcjs('other.rotateRight()'), at(0, 8)), undefined);
    assert.equal(hints.hoverProvider.provideHover(stcjs('const rotateRight = 1;'), at(0, 8)), undefined);
    assert.equal(hints.hoverProvider.provideHover(js('stencil.crop()'), at(0, 10)), undefined);
  });
});

test('the two toggles are read per request, so neither needs a reload', async () => {
  await withHost({ 'stencil.completion': false }, ({ hints }) => {
    assert.deepEqual(hints.completionProvider.provideCompletionItems(stcjs('stencil.'), at(0, 8)), []);
    assert.ok(hints.hoverProvider.provideHover(stcjs('stencil.crop()'), at(0, 10)));
  });
  await withHost({ 'stencil.hover': false }, ({ hints }) => {
    assert.equal(hints.hoverProvider.provideHover(stcjs('stencil.crop()'), at(0, 10)), undefined);
    assert.ok(hints.completionProvider.provideCompletionItems(stcjs('stencil.'), at(0, 8)).length > 0);
  });
});

test('both providers are registered for both flavours, and `.` triggers the list', async () => {
  await withHost({}, ({ calls, hints }) => {
    const context = { subscriptions: [] };
    hints.register(context);
    const completion = calls.completionProviders.at(-1);
    assert.deepEqual(completion.selector, [{ language: 'stencil-js' }, { language: 'javascript' }]);
    assert.deepEqual(completion.triggers, ['.']);
    assert.deepEqual(calls.hoverProviders.at(-1).selector, completion.selector);
    assert.equal(context.subscriptions.length, 2);
  });
});

// Reported from a real editor: hovering the bare `stencil` fell through to TypeScript's
// `any`, because only members were answered for.
test('hovering the global itself explains the facade, not `any`', async () => {
  await withHost({}, ({ hints }) => {
    const hover = hints.hoverProvider.provideHover(stcjs('stencil.rotateRight()'), at(0, 3));
    assert.match(hover.contents.value, /\*\*`stencil`\*\* — /);
    assert.match(hover.contents.value, /console control API/);
    // Through `window.` too, and still nothing for a name that only looks like it.
    assert.ok(hints.hoverProvider.provideHover(stcjs('window.stencil.undo()'), at(0, 9)));
    assert.equal(hints.hoverProvider.provideHover(stcjs('const myStencil = 1'), at(0, 9)), undefined);
    // And a member still wins over the global on the same line.
    const member = hints.hoverProvider.provideHover(stcjs('stencil.rotateRight()'), at(0, 12));
    assert.match(member.contents.value, /\*\*stencil\.rotateRight\*\*/);
  });
});

// With the types installed the editor answers the same prose, so answering too doubles it.
test('the extension stands aside in a .js the workspace has typed, and only there', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-hints-'));
  try {
    await withHost({}, ({ hints }) => {
      // Before the types are installed the extension is the only answer a .js gets.
      const opted = js('// @use stencil\nstencil.crop()');
      assert.ok(hints.completionProvider.provideCompletionItems(opted, at(1, 8)).length > 100);
      assert.ok(hints.hoverProvider.provideHover(opted, at(1, 3)));
    }, dir);
    writeFileSync(join(dir, 'stencil.d.ts'), 'declare const stencil: unknown;\n');
    await withHost({}, ({ hints }) => {
      const opted = js('// @use stencil\nstencil.crop()');
      assert.equal(hints.typescriptAnswers(opted), true);
      assert.deepEqual(hints.completionProvider.provideCompletionItems(opted, at(1, 8)), []);
      assert.equal(hints.hoverProvider.provideHover(opted, at(1, 3)), undefined);
      // The .stcjs keeps both, because TypeScript claims no such language.
      const own = stcjs('stencil.crop()');
      assert.equal(hints.typescriptAnswers(own), false);
      assert.ok(hints.completionProvider.provideCompletionItems(own, at(0, 8)).length > 100);
      assert.ok(hints.hoverProvider.provideHover(own, at(0, 3)));
    }, dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
