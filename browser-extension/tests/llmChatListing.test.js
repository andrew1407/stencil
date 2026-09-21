// The pure halves of src/llm/chatController.js: the §8 context listing (100-entry cap,
// name/alt truncation), the open.actions → launch-option table, and the §7 replay rule.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { formatOfItem } from '../src/lib/highlight/filters.js';
import {
  LISTING_LIMIT, LISTING_NAME_CHARS, LISTING_ALT_CHARS,
  buildListing, listingKind, translateOpenActions, replayMessages, splitDataUrl,
} from '../src/llm/chatController.js';

// ── Listing (contract §8) ──

test('listingKind maps scan records onto the §8 categories', () => {
  assert.equal(listingKind({ kind: 'img', src: 'x' }), 'img');
  assert.equal(listingKind({ kind: 'bg', src: 'x' }), 'background');
  assert.equal(listingKind({ kind: 'video', videoUrl: 'x' }), 'video');
  assert.equal(listingKind({ kind: 'img', src: 'x', poster: true }), 'poster');
  assert.equal(listingKind({ kind: 'img', src: 'x', meta: true }), 'icon');
});

test('buildListing lines carry index, kind, dims, format, basename, alt', () => {
  const items = [
    { kind: 'img', src: 'https://a.example/photos/cat.png', w: 800, h: 600, alt: 'A cat' },
    { kind: 'bg', src: 'https://a.example/tiles/bg.jpg', w: 0, h: 0, alt: '' },
    { kind: 'video', src: 'data:image/jpeg;base64,FRAME', videoUrl: 'https://a.example/v/clip.mp4', w: 1920, h: 1080, alt: 'video' },
    { kind: 'img', src: 'https://a.example/favicon.ico', w: 0, h: 0, alt: 'icon', meta: true },
  ];
  const lines = buildListing(items, { formatOfItem }).split('\n');
  assert.equal(lines[0], '0: img 800x600 png "cat.png" alt "A cat"');
  assert.equal(lines[1], '1: background jpg "bg.jpg"');            // no dims/alt when unknown
  assert.equal(lines[2], '2: video 1920x1080 mp4 "clip.mp4" alt "video"');   // video keys on its media URL
  assert.equal(lines[3], '3: icon ico "favicon.ico" alt "icon"');
});

test('buildListing truncates to 100 entries and notes the overflow', () => {
  const items = Array.from({ length: 105 }, (_, i) => ({ kind: 'img', src: `https://a.example/i${i}.png`, w: 0, h: 0 }));
  const lines = buildListing(items, { formatOfItem }).split('\n');
  assert.equal(lines.length, LISTING_LIMIT + 1);
  assert.equal(lines[99], '99: img png "i99.png"');
  assert.equal(lines[100], '(+5 more not listed)');
});

test('buildListing truncates long names and alt text', () => {
  const longName = 'x'.repeat(90) + '.png';
  const longAlt = 'y'.repeat(200);
  const [line] = buildListing(
    [{ kind: 'img', src: `https://a.example/${longName}`, w: 0, h: 0, alt: longAlt }],
    { formatOfItem },
  ).split('\n');
  const name = /"([^"]*)" alt "([^"]*)"$/.exec(line);
  assert.ok(name, line);
  assert.equal(name[1].length, LISTING_NAME_CHARS);
  assert.ok(name[1].endsWith('…'));
  assert.equal(name[2].length, LISTING_ALT_CHARS);
  assert.ok(name[2].endsWith('…'));
});

test('buildListing handles data: sources and in-page videos gracefully', () => {
  const lines = buildListing([
    { kind: 'img', src: 'data:image/png;base64,AAAA', w: 4, h: 4 },
    { kind: 'video', src: '', videoUrl: '', w: 0, h: 0 },
  ], { formatOfItem }).split('\n');
  assert.match(lines[0], /"\(inline data\)"/);
  assert.match(lines[1], /"\(in-page video\)"/);
});

// ── open.actions → launch options (contract §8 translation) ──

test('crop: % / px / bare / negative tokens resolve against the image dims', () => {
  const dims = { width: 1000, height: 500 };
  const { launch, warnings } = translateOpenActions(
    [{ op: 'crop', spec: { x1: '10%', x2: '-10%', y1: '50', y2: '400px' } }], dims);
  assert.deepEqual(warnings, []);
  // Launch crops leave in the canonical {w,h} wire spelling.
  assert.deepEqual(launch.crop, { x: 100, y: 50, w: 800, h: 350 });
});

test('crop: unspecified edges default to the full image; later crops keep earlier edges', () => {
  const dims = { width: 1000, height: 500 };
  const one = translateOpenActions([{ op: 'crop', spec: { x1: '200' } }], dims);
  assert.deepEqual(one.launch.crop, { x: 200, y: 0, w: 800, h: 500 });
  const two = translateOpenActions(
    [{ op: 'crop', spec: { x1: '200' } }, { op: 'crop', spec: { y2: '-100' } }], dims);
  assert.deepEqual(two.launch.crop, { x: 200, y: 0, w: 800, h: 400 });
});

test('crop: cm/in tokens cannot resolve in the extension → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions(
    [{ op: 'crop', spec: { x1: '2cm' } }], { width: 1000, height: 500 });
  assert.equal(launch.crop, undefined);
  assert.match(warnings[0], /cm\/in/);
});

test('crop: unknown image dimensions → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions([{ op: 'crop', spec: { x1: '10%' } }], {});
  assert.equal(launch.crop, undefined);
  assert.match(warnings[0], /dimensions/);
});

test('rotate has no launch-payload equivalent → dropped with a warning', () => {
  const { launch, warnings } = translateOpenActions(
    [{ op: 'rotate', dir: 'left', times: 2 }], { width: 10, height: 10 });
  assert.deepEqual(launch, {});
  assert.match(warnings[0], /rotate/i);
});

test('filter and layout fold into one layout payload (imageFilter/filterColor/lines)', () => {
  const lines = [{ points: [{ x: 0, y: 0 }, { x: 5, y: 5 }], color: '#FFFF00' }];
  const { launch, warnings } = translateOpenActions([
    { op: 'filter', mode: 'custom', tint: '#12ab34' },
    { op: 'layout', lines },
  ], { width: 640, height: 480 });
  assert.deepEqual(warnings, []);
  assert.deepEqual(launch.layout, {
    lines, imageFilter: 'custom', filterColor: '#12ab34', imageWidth: 640, imageHeight: 480,
  });
});

test('a non-custom filter carries no filterColor; two layouts concatenate lines', () => {
  const l1 = [{ points: [{ x: 0, y: 0 }] }];
  const l2 = [{ points: [{ x: 1, y: 1 }] }];
  const { launch } = translateOpenActions([
    { op: 'layout', lines: l1 },
    { op: 'filter', mode: 'bw' },
    { op: 'layout', lines: l2 },
  ], { width: 10, height: 10 });
  assert.equal(launch.layout.imageFilter, 'bw');
  assert.equal(launch.layout.filterColor, undefined);
  assert.deepEqual(launch.layout.lines, [...l1, ...l2]);
});

test('page maps to the launch page slot with the canonical uppercase name', () => {
  const { launch } = translateOpenActions([{ op: 'page', format: 'a4' }], { width: 10, height: 10 });
  assert.deepEqual(launch.page, { size: 'A4' });
  assert.deepEqual(translateOpenActions([{ op: 'page', format: 'b10' }], {}).launch.page, { size: 'B10' });
});

// ── §7 image replay rule ──

test('replayMessages keeps the current turn images + only the most recent prior image', () => {
  const img = (d) => ({ mediaType: 'image/png', data: d });
  const history = [
    { role: 'user', text: 'a', images: [img('OLD')] },
    { role: 'assistant', text: 'r1' },
    { role: 'user', text: 'b', images: [img('P1'), img('P2')] },
    { role: 'assistant', text: 'r2' },
    { role: 'user', text: 'c', images: [img('NOW1'), img('NOW2')] },
  ];
  const out = replayMessages(history);
  assert.equal(out[0].images, undefined);                       // older turn → text-only
  assert.deepEqual(out[2].images, [img('P2')]);                 // most recent prior → last image only
  assert.deepEqual(out[4].images, [img('NOW1'), img('NOW2')]);  // current turn → all images
});

test('splitDataUrl parses the LlmImage wire shape', () => {
  assert.deepEqual(splitDataUrl('data:image/png;base64,AAAA'), { mediaType: 'image/png', data: 'AAAA' });
  assert.equal(splitDataUrl('https://not-a-data-url'), null);
});
