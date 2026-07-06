// Mirror-equality guard for the MAIN-world window.stencil script.
//
// src/lib/pageImages.js is the unit-tested SOURCE OF TRUTH for a handful of pure
// helpers (bgImageUrl / nameFromUrl / videoHasFrame). src/content/pageApiMain.js runs
// in the page's MAIN world, which can't import ES modules, so it carries an inline
// MIRROR of those same helpers. Nothing forces the two to agree — a fix applied to one
// copy but not the other would silently drift.
//
// This test extracts each inline copy's source out of pageApiMain.js, evaluates it, and
// asserts it produces IDENTICAL output to the pageImages.js export across a battery of
// inputs (behavioral equality — the copies are allowed to differ textually, e.g. a
// ternary vs an early return, as long as they behave the same). If either copy's
// behavior diverges, CI fails here.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import * as truth from '../src/lib/pageImages.js';

const MAIN_SRC = readFileSync(
  fileURLToPath(new URL('../src/content/pageApiMain.js', import.meta.url)),
  'utf8',
);

// Pull `const <name> = <arrow expression>;` out of the MAIN-world IIFE and evaluate it
// into a callable. The helpers are declared at the IIFE's top level (2-space indent);
// block-bodied ones close on a line that is exactly `  };`, single-expression ones on
// the first `;`. Returns the live function so we can run inputs through it.
const extractFn = (name, { block }) => {
  const re = block
    ? new RegExp(`\\n  const ${name} = ([\\s\\S]*?\\n  };)`)
    : new RegExp(`\\n  const ${name} = ([\\s\\S]*?;)`);
  const m = re.exec(MAIN_SRC);
  assert.ok(m, `could not locate the inline copy of ${name} in pageApiMain.js`);
  // eslint-disable-next-line no-new-func
  return new Function(`return (${m[1].replace(/;\s*$/, '')});`)();
};

const mainBgImageUrl = extractFn('bgImageUrl', { block: true });
const mainNameFromUrl = extractFn('nameFromUrl', { block: true });
const mainVideoHasFrame = extractFn('videoHasFrame', { block: false });

// Assert the two copies agree on every input in `cases`.
const assertAgree = (label, truthFn, mainFn, cases) => {
  for (const args of cases) {
    const expected = truthFn(...args);
    const actual = mainFn(...args);
    assert.deepEqual(
      actual, expected,
      `${label}(${args.map((a) => JSON.stringify(a)).join(', ')}): ` +
      `pageApiMain copy → ${JSON.stringify(actual)}, pageImages source of truth → ${JSON.stringify(expected)}`,
    );
  }
};

test('bgImageUrl: inline MAIN-world copy matches the pageImages.js source of truth', () => {
  assertAgree('bgImageUrl', truth.bgImageUrl, mainBgImageUrl, [
    ['url("https://a.com/x.png")'],
    ["url('https://a.com/y.jpg')"],
    ['url(https://a.com/z.gif)'],
    ['url(  https://a.com/spaced.png  )'],
    ['url("data:image/png;base64,AAAA")'],
    ['url(data:image/svg+xml;base64,AAAA)'],
    ["url('data:image/svg+xml;utf8,<svg/>')"],
    ['none'],
    ['linear-gradient(#000, #fff)'],
    [''],
    [null],
    [undefined],
    [0],
    ['URL("https://a.com/UPPER.png")'],
  ]);
});

test('nameFromUrl: inline MAIN-world copy matches the pageImages.js source of truth', () => {
  assertAgree('nameFromUrl', truth.nameFromUrl, mainNameFromUrl, [
    ['https://a.com/pics/cat.png?v=2'],
    ['https://a.com/pics/cat.png?v=2', 'video'],
    ['https://a.com/no-ext'],
    ['https://a.com/no-ext', 'video'],
    ['https://a.com/'],
    ['https://a.com/deep/path/to/photo.JPEG#frag'],
    ['https://a.com/name%20with%20spaces.png'],
    ['data:image/jpeg;base64,AAAA'],
    ['data:image/jpeg;base64,AAAA', 'video'],
    ['data:image/svg+xml;base64,AAAA'],
    ['data:text/plain,hi'],
    ['not a url'],
    ['not a url', 'clip'],
    [''],
    [null],
    [undefined],
  ]);
});

test('videoHasFrame: inline MAIN-world copy matches the pageImages.js source of truth', () => {
  assertAgree('videoHasFrame', truth.videoHasFrame, mainVideoHasFrame, [
    [{ videoWidth: 640, videoHeight: 480, readyState: 2, paused: false, currentTime: 3 }],
    [{ videoWidth: 640, videoHeight: 480, readyState: 1, paused: false, currentTime: 3 }],
    [{ videoWidth: 640, videoHeight: 480, readyState: 4, paused: true, currentTime: 0 }],
    [{ videoWidth: 640, videoHeight: 480, readyState: 4, paused: true, currentTime: 2 }],
    [{ videoWidth: 0, videoHeight: 0, readyState: 4, paused: false, currentTime: 1 }],
    [{ videoWidth: 640, videoHeight: 0, readyState: 2, paused: false, currentTime: 1 }],
    [{ videoWidth: 640, videoHeight: 480, readyState: 2, paused: false, currentTime: 0 }],
    [null],
    [undefined],
    [{}],
  ]);
});
