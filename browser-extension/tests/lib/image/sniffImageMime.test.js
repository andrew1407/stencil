// The magic-byte type check (src/lib/image/data.js sniffImageMime) a page-API fetch refuses
// non-images by, walked over the shared image-header corpus: every case the sniffers measure
// is typed by its format, and bytes that are no image type as nothing.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { sniffImageMime } from '../../../src/lib/image/data.js';

const corpus = JSON.parse(readFileSync(new URL('../../../../common/fixtures/imageHeader/cases.json', import.meta.url), 'utf8'));
const cases = Array.isArray(corpus) ? corpus : corpus.cases;
const bytes = (b64) => { const u = Buffer.from(b64, 'base64'); return u.buffer.slice(u.byteOffset, u.byteOffset + u.byteLength); };
const ascii = (s) => bytes(Buffer.from(s, 'latin1').toString('base64'));

test('every measured corpus case is typed by its format', () => {
  const measured = cases.filter((c) => c.expect);
  assert.ok(measured.length > 20, 'the corpus was read');
  for (const c of measured) assert.equal(sniffImageMime(bytes(c.base64)), `image/${c.expect.format}`, c.name);
});

test('the types the corpus does not carry, and bytes that are no image', () => {
  assert.equal(sniffImageMime(bytes('AAABAAEAEBA=')), 'image/x-icon');
  assert.equal(sniffImageMime(ascii('II*\0\x08\0\0\0')), 'image/tiff');
  assert.equal(sniffImageMime(ascii('MM\0*\0\0\0\x08')), 'image/tiff');
  assert.equal(sniffImageMime(ascii('\0\0\0\x1cftypavif\0\0\0\0')), 'image/avif');
  assert.equal(sniffImageMime(ascii('\0\0\0\x18ftypmp42\0\0\0\0')), '', 'an mp4 is no image');
  assert.equal(sniffImageMime(ascii('<!doctype html><html>')), '');
  assert.equal(sniffImageMime(ascii('RIFF\0\0\0\0WAVEfmt ')), '', 'a RIFF that is no WebP');
  assert.equal(sniffImageMime(new ArrayBuffer(0)), '');
});
