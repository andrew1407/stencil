// Tests for src/lib/entry.js — normalising media DROPPED on the panel (a page
// <img>/<video> dragged out of the page, a local file, a data:/blob: source) into the
// same scan-row shape the ⋯ action menu already consumes, so the header logo's drop
// target reuses the row machinery instead of growing its own. Pure: the object-URL
// seam is injected, so this runs under plain `node --test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  sameSource, isMediaFile, entryFromUrl, entryFromDrop,
  DRAG_MENU_ACTIONS, dragMenuActions, dragActionAllowed,
  INTERNAL_DRAG_TYPE, dragPayloadKind, createDragArmer,
} from '../../../src/lib/drop/entry.js';

const ids = (entry) => dragMenuActions(entry).map((a) => a.id);

const objectUrl = () => 'blob:chrome-extension://ext/abc';

// ── sameSource ──

test('sameSource ignores only the #fragment', () => {
  assert.equal(sameSource('https://a.example/c.png', 'https://a.example/c.png'), true);
  assert.equal(sameSource('https://a.example/c.png#x', 'https://a.example/c.png'), true);
  assert.equal(sameSource('https://a.example/c.png?v=2', 'https://a.example/c.png'), false);
  assert.equal(sameSource('data:image/png;base64,AA', 'data:image/png;base64,AA'), true);
  assert.equal(sameSource('', 'https://a.example/c.png'), false);
  assert.equal(sameSource(undefined, undefined), false);
});

// ── entryFromUrl: kind + name derivation ──

test('an image URL becomes an img row named from its path', () => {
  const e = entryFromUrl('https://a.example/photos/cat.png?v=3');
  assert.equal(e.kind, 'img');
  assert.equal(e.src, 'https://a.example/photos/cat.png?v=3');
  assert.equal(e.name, 'cat.png');
  assert.deepEqual([e.w, e.h], [0, 0]);
  assert.equal(e.measured, false);           // dims unknown → measured lazily
  assert.deepEqual(e.opened, []);
  assert.equal(e.pinned, false);
});

test('a video URL becomes a frameless video row keyed on its media URL', () => {
  const e = entryFromUrl('https://a.example/v/clip.mp4');
  assert.equal(e.kind, 'video');
  assert.equal(e.videoUrl, 'https://a.example/v/clip.mp4');
  assert.equal(e.src, '', 'no captured still — exactly how the scanner lists one');
  assert.equal(e.name, 'clip.mp4');
  assert.equal(e.measured, true, 'nothing to measure');
});

test('data: and blob: sources still get a usable name and kind', () => {
  const svg = entryFromUrl('data:image/svg+xml;base64,PHN2Zy8+');
  assert.equal(svg.kind, 'img');
  assert.equal(svg.name, 'image.svg');       // data: URLs name from their media type
  const blob = entryFromUrl('blob:https://a.example/9f2c-1234');
  assert.equal(blob.kind, 'img');
  assert.ok(blob.name.endsWith('.png'));     // no extension to read → a sane default
  assert.equal(entryFromUrl(''), null);
  assert.equal(entryFromUrl(null), null);
});

test('an explicit kind/name (a dropped File knows both) wins over the URL guess', () => {
  const e = entryFromUrl('blob:chrome-extension://ext/abc', { name: 'holiday.mov', kind: 'video' });
  assert.equal(e.kind, 'video');
  assert.equal(e.videoUrl, 'blob:chrome-extension://ext/abc');
  assert.equal(e.name, 'holiday.mov');
});

// ── entryFromDrop ──

test('a dropped URL that matches a SCANNED row reuses that richer row', () => {
  const scanned = {
    kind: 'img', src: 'https://a.example/cat.png', name: 'cat.png',
    w: 800, h: 600, measured: true, opened: [{ count: 2 }], pinned: true,
  };
  const items = [{ kind: 'img', src: 'https://a.example/other.png' }, scanned];
  const e = entryFromDrop({ kind: 'url', url: 'https://a.example/cat.png#frag' }, { items });
  assert.equal(e, scanned, 'the same object — dims, opened badge and pin state come along');
});

test('a video row is matched on its media URL, not its still', () => {
  const scanned = { kind: 'video', src: 'data:image/jpeg;base64,FRAME', videoUrl: 'https://a.example/clip.mp4', name: 'clip.mp4' };
  const e = entryFromDrop({ kind: 'url', url: 'https://a.example/clip.mp4' }, { items: [scanned] });
  assert.equal(e, scanned);
});

test('a URL that is not in the scan becomes a fresh entry', () => {
  const e = entryFromDrop({ kind: 'url', url: 'https://b.example/logo.svg' }, { items: [{ kind: 'img', src: 'https://a.example/x.png' }] });
  assert.equal(e.kind, 'img');
  assert.equal(e.src, 'https://b.example/logo.svg');
  assert.equal(e.name, 'logo.svg');
});

test('a dropped FILE is taken through an object URL, with its MIME picking the kind', () => {
  const img = entryFromDrop({ kind: 'files', files: [{ name: 'shot.png', type: 'image/png' }] }, { objectUrl });
  assert.equal(img.kind, 'img');
  assert.equal(img.src, 'blob:chrome-extension://ext/abc');
  assert.equal(img.name, 'shot.png');

  const vid = entryFromDrop({ kind: 'files', files: [{ name: 'clip.webm', type: 'video/webm' }] }, { objectUrl });
  assert.equal(vid.kind, 'video');
  assert.equal(vid.videoUrl, 'blob:chrome-extension://ext/abc');
  assert.equal(vid.name, 'clip.webm');
});

test('non-media files are skipped; the first media file wins', () => {
  const files = [{ name: 'notes.txt', type: 'text/plain' }, { name: 'a.jpg', type: 'image/jpeg' }];
  assert.equal(entryFromDrop({ kind: 'files', files }, { objectUrl }).name, 'a.jpg');
  assert.equal(entryFromDrop({ kind: 'files', files: [files[0]] }, { objectUrl }), null);
  assert.equal(isMediaFile({ name: 'a.mkv', type: '' }), true, 'extension fallback for a typeless video');
  assert.equal(isMediaFile(null), false);
});

test('nothing usable → null (never a half-built entry)', () => {
  assert.equal(entryFromDrop(null), null);
  assert.equal(entryFromDrop({ kind: 'url', url: '   ' }), null);
  assert.equal(entryFromDrop({ kind: 'files', files: [] }, { objectUrl }), null);
  // Files need the object-URL seam; without it there is no source to act on.
  assert.equal(entryFromDrop({ kind: 'files', files: [{ name: 'a.png', type: 'image/png' }] }), null);
});

// ── The spring-loaded drag menu's four actions ──

test('the drag menu is exactly four flat actions, in order', () => {
  assert.deepEqual(DRAG_MENU_ACTIONS.map((a) => a.id), ['editor', 'newtab', 'incognito', 'crop']);
  assert.deepEqual(DRAG_MENU_ACTIONS.map((a) => a.label),
    ['Open in editor', 'Open in new tab', 'Open incognito', 'Crop']);
  // Only "open in new tab" works without something drawable.
  assert.deepEqual(DRAG_MENU_ACTIONS.filter((a) => !a.needsPixels).map((a) => a.id), ['newtab']);
});

test('an UNKNOWN-dimension image drag keeps all four actions', () => {
  assert.deepEqual(ids(entryFromUrl('https://a.example/cat.png')), ['editor', 'newtab', 'incognito', 'crop']);
});

test('a frameless video drag keeps only "open in new tab"', () => {
  const video = entryFromUrl('https://a.example/clip.mp4');
  assert.deepEqual(ids(video), ['newtab']);
  assert.equal(dragActionAllowed(video, 'newtab'), true);
  assert.equal(dragActionAllowed(video, 'editor'), false, 'no still to open in the editor');
  assert.equal(dragActionAllowed(video, 'crop'), false);
  assert.equal(dragActionAllowed(video, 'incognito'), false);
});

test('a video WITH a captured still gets the pixel actions back', () => {
  const framed = { kind: 'video', src: 'data:image/jpeg;base64,FRAME', videoUrl: 'https://a.example/clip.mp4' };
  assert.deepEqual(ids(framed), ['editor', 'newtab', 'incognito', 'crop']);
});

test('an unreadable payload (a dragover exposes types, never data) springs the full menu', () => {
  // The menu opens MID-DRAG, before the payload can be read — so it is optimistic and
  // the drop re-checks the real entry.
  assert.deepEqual(ids(null), ['editor', 'newtab', 'incognito', 'crop']);
  assert.deepEqual(ids(undefined), ['editor', 'newtab', 'incognito', 'crop']);
});

test('an entry with no source at all offers nothing', () => {
  assert.deepEqual(ids({ kind: 'img', src: '' }), []);
  assert.equal(dragActionAllowed({ kind: 'img', src: '' }, 'newtab'), false);
});

// ── What is being dragged (types only — all a dragover exposes) ──

test('dragPayloadKind reads the DataTransfer types', () => {
  assert.equal(dragPayloadKind([INTERNAL_DRAG_TYPE, 'text/uri-list']), 'internal');
  assert.equal(dragPayloadKind(['Files']), 'files');
  assert.equal(dragPayloadKind(['text/uri-list', 'text/html']), 'url');
  assert.equal(dragPayloadKind(['text/plain']), 'url');
  assert.equal(dragPayloadKind(['application/x-something']), '', 'not media we can act on');
  assert.equal(dragPayloadKind([]), '');
  assert.equal(dragPayloadKind(null), '');
  // DataTransfer.types is array-LIKE, not an Array.
  assert.equal(dragPayloadKind(new Set(['Files'])), 'files');
});

// ── The logo's "a drop target is live" advertisement ──

test('a compatible drag arms the logo, and the drag ending disarms it', () => {
  const seen = [];
  const armer = createDragArmer({ setArmed: (on) => seen.push(on) });
  assert.equal(armer.isArmed(), false);

  armer.update(['text/uri-list']);            // a page image is being dragged
  assert.equal(armer.isArmed(), true);
  assert.deepEqual(seen, [true]);

  armer.end();                                 // dragend / drop / left the window
  assert.equal(armer.isArmed(), false);
  assert.deepEqual(seen, [true, false]);
});

test('the class is only toggled on a CHANGE (dragover fires continuously)', () => {
  const seen = [];
  const armer = createDragArmer({ setArmed: (on) => seen.push(on) });
  for (let i = 0; i < 8; i++) armer.update([INTERNAL_DRAG_TYPE, 'text/uri-list']);
  assert.deepEqual(seen, [true], 'one toggle, not one per event');
  armer.end();
  armer.end();
  assert.deepEqual(seen, [true, false]);
});

test('an incompatible drag never arms it', () => {
  const seen = [];
  const armer = createDragArmer({ setArmed: (on) => seen.push(on) });
  armer.update(['application/x-something']);
  armer.update([]);
  assert.equal(armer.isArmed(), false);
  assert.deepEqual(seen, []);
});
