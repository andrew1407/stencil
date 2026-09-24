// Where the marker may stand — anywhere, on a comment line of its own — and what a line is
// told when it merely carries the words.
import test from 'node:test';
import assert from 'node:assert/strict';

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';


import { at, js, span, stcjs, withHost } from '../../helpers/jsHost.js';

import * as jsSource from '../../../src/lib/emit/jsSource.js';

// A comment: the editor's own service never explains one, so this answer is given even where
// the extension otherwise stands aside.
test('the marker explains itself, typed workspace or not', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'stencil-marker-'));
  try {
    await withHost({}, ({ hints }) => {
      const opted = js('// @use stencil\nstencil.crop()');
      const onUse = hints.hoverProvider.provideHover(opted, at(0, 4));
      assert.match(onUse.contents.value, /^\*\*The Stencil marker\*\* — /);
      // Fenced, like every other tooltip here: a hover colours a code block, not inline code.
      assert.match(onUse.contents.value, /```js\n\/\/ @use stencil\n```/);
      assert.match(onUse.contents.value, /may stand \*\*anywhere\*\*/);
      assert.match(onUse.contents.value, /Add facade typings/);
      assert.ok(!onUse.contents.value.includes('rather than'), 'the type talk belongs elsewhere');
      // The whole marker answers, and the editor is given that range to underline.
      assert.ok(hints.hoverProvider.provideHover(opted, at(0, 12)));
      assert.deepEqual(span(onUse), [0, 3, 0, 15]);
      const indented = js('  // @use stencil — with prose after it\nstencil.crop()');
      assert.deepEqual(span(hints.hoverProvider.provideHover(indented, at(0, 8))), [0, 5, 0, 17]);
      // …and nothing before the `@` is the marker.
      assert.equal(hints.hoverProvider.provideHover(opted, at(0, 1)), undefined);
    });
    writeFileSync(join(dir, 'stencil.d.ts'), 'declare const stencil: unknown;\n');
    await withHost({}, ({ hints }) => {
      const opted = js('// @use stencil\nstencil.crop()');
      assert.ok(hints.hoverProvider.provideHover(opted, at(0, 4)), 'still answered');
      assert.equal(hints.hoverProvider.provideHover(opted, at(1, 3)), undefined, 'but the code is not');
    }, dir);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// `stencil` written in a sentence is prose. A URL is not a comment, whatever its slashes.
test('a word in a line comment pops nothing, and a URL is not a comment', async () => {
  await withHost({}, ({ hints }) => {
    const prose = stcjs('// try stencil.crop() when a picture is open\nstencil.crop()');
    assert.equal(hints.hoverProvider.provideHover(prose, at(0, 9)), undefined);
    assert.equal(hints.hoverProvider.provideHover(prose, at(0, 16)), undefined);
    assert.ok(hints.hoverProvider.provideHover(prose, at(1, 3)), 'the code still answers');
    const url = stcjs("await stencil.load('https://example.com/a.png');");
    assert.ok(hints.hoverProvider.provideHover(url, at(0, 15)), 'a member after a URL still answers');
  });
});

// A `//` is only a comment outside a string, so a URL is one case of a general rule and an
// escaped quote does not end the literal that hides one.
test('slashes inside a string open no comment, whatever follows them', async () => {
  await withHost({}, ({ hints }) => {
    const { commentStart } = jsSource;
    assert.equal(commentStart('stencil.crop() // then save'), 15);
    assert.equal(commentStart("const s = 'a//b'; stencil.crop()"), -1);
    assert.equal(commentStart('const s = "it\\" // not"; stencil.crop()'), -1);
    assert.equal(commentStart('const t = `a//b`; stencil.crop()'), -1);
    assert.equal(commentStart('nothing here'), -1);
    // A line ending inside a quote could not be followed, so the plain reading stands in.
    assert.equal(commentStart('const re = /[\'"]/; // note'), 19);
    assert.equal(commentStart('const s = "abc // def'), 15);
    const hidden = stcjs('const s = "a//b"; stencil.crop()');
    assert.ok(hints.hoverProvider.provideHover(hidden, at(0, 20)), 'the code after it answers');
    assert.ok(hints.completionProvider.provideCompletionItems(
      stcjs('const s = "a//b"; stencil.'), at(0, 26)).length > 100);
  });
});

// The list reads a comment the way the hover does: writing about the facade is not calling it.
test('the member list is not offered inside a comment either', async () => {
  await withHost({}, ({ hints }) => {
    const prose = stcjs('// see stencil.\nstencil.');
    assert.deepEqual(hints.completionProvider.provideCompletionItems(prose, at(0, 15)), []);
    assert.ok(hints.completionProvider.provideCompletionItems(prose, at(1, 8)).length > 100);
    const url = stcjs("await stencil.load('https://example.com/');\nstencil.");
    assert.ok(hints.completionProvider.provideCompletionItems(url, at(1, 8)).length > 100,
      'a URL on an earlier line is not a comment');
  });
});

// Finding a marker that may stand anywhere reads the whole buffer, and every keystroke asks.
test('the marker is read once per edit, not once per keystroke', async () => {
  await withHost({}, ({ hints }) => {
    let reads = 0;
    const document = {
      languageId: 'javascript',
      uri: { toString: () => 'file:///tmp/a.js' },
      version: 1,
      getText() { reads += 1; return '// @use stencil\nstencil.'; },
      lineAt: (line) => ({ text: this?.text ?? 'stencil.' }),
    };
    for (let i = 0; i < 5; i += 1) hints.completionProvider.provideCompletionItems(document, at(1, 8));
    assert.equal(reads, 1, 'the buffer was scanned once for one version');
    document.version = 2;
    hints.completionProvider.provideCompletionItems(document, at(1, 8));
    assert.equal(reads, 2, 'an edit is read again');
    const unversioned = { ...document, version: undefined };   // nothing would invalidate it
    hints.completionProvider.provideCompletionItems(unversioned, at(1, 8));
    hints.completionProvider.provideCompletionItems(unversioned, at(1, 8));
    assert.equal(reads, 4);
  });
});

// Anywhere: under a header, below the code, at the very end, indented inside a function.
test('the marker is read wherever in the file it stands', async () => {
  await withHost({}, ({ hints }) => {
    const files = {
      'first line': '// @use stencil\nstencil.crop()',
      'under a header': '// Copyright 2026 Someone\n//\n/* a block, even */\n// @use stencil\n\nstencil.crop()',
      'below the code': 'stencil.crop()\n// @use stencil',
      'at the very end': 'const a = 1;\nconst b = 2;\nstencil.crop()\n// @use stencil',
      'indented, inside a function': 'function f() {\n  stencil.crop()\n  // @use stencil\n}',
    };
    for (const [where, text] of Object.entries(files)) {
      const doc = js(text);
      const lines = text.split('\n');
      const line = lines.findIndex((l) => l.includes('stencil.crop'));
      const caret = lines[line].indexOf('stencil.') + 'stencil.'.length;
      assert.ok(hints.completionProvider.provideCompletionItems(doc, at(line, caret)).length > 100,
        `${where}: the file opted in`);
    }
  });
});

// The words alone are not the marker: code in front of them means it is not read.
test('the words with code in front of them are explained, not obeyed', async () => {
  await withHost({}, ({ hints }) => {
    const trailing = js('const a = 1; // @use stencil\nstencil.crop()');
    assert.deepEqual(hints.completionProvider.provideCompletionItems(trailing, at(1, 8)), [],
      'the file never opted in');
    const explained = hints.hoverProvider.provideHover(trailing, at(0, 20));
    assert.match(explained.contents.value, /not read here/);
    assert.match(explained.contents.value, /its own line/);
    assert.deepEqual(span(explained), [0, 16, 0, 28], 'the words light up, wherever they sit');
    // Inside a string it is not even the words: nothing is said, and nothing is opted in.
    const quoted = js('const s = "// @use stencil";\nstencil.crop()');
    assert.deepEqual(hints.completionProvider.provideCompletionItems(quoted, at(1, 8)), []);
  });
});

// A .stcjs needs no marker, and its tooltip says so wherever one is written.
test('a .stcjs reads a marker anywhere as the ordinary one', async () => {
  await withHost({}, ({ hints }) => {
    const own = stcjs('stencil.crop();\n// @use stencil');
    const explained = hints.hoverProvider.provideHover(own, at(1, 5));
    assert.match(explained.contents.value, /^\*\*The Stencil marker\*\* — /);
    assert.match(explained.contents.value, /needs no marker at all/);
  });
});
