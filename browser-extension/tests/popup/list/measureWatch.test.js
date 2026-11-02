// popup/list/measureWatch.js — the image list's lazy size probe lets go of every row it watched: a row a
// render dropped, a leaving ghost and a measured row all leave the observer, so its target count returns
// to the rows on screen however often a long list is scrolled or re-filtered.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createMeasureWatch } from '../../../src/popup/list/measureWatch.js';

const rig = () => {
  const targets = new Set();
  let report = null;
  const probes = [];
  const sizes = createMeasureWatch({
    makeObserver: (onEntries) => {
      report = onEntries;
      return { observe: (el) => targets.add(el), unobserve: (el) => targets.delete(el) };
    },
    measure: (image, li, done) => probes.push({ image, li, done }),
  });
  const intersect = (li) => report([{ target: li, isIntersecting: true }]);
  return { sizes, targets, probes, intersect };
};
const row = () => ({ isConnected: true });
const image = (measured = false) => ({ measured });

test('rows a render dropped leave the observer at the next sweep', () => {
  const { sizes, targets } = rig();
  const rows = Array.from({ length: 40 }, row);
  rows.forEach((li) => sizes.watch(li, image()));
  assert.equal(targets.size, 40);
  for (const li of rows.slice(0, 30)) li.isConnected = false;
  sizes.sweep();
  assert.equal(targets.size, 10, 'only the rows still in the list are observed');
  assert.equal(sizes.size, 10);
});

test('scrolling a long list back and forth keeps the observer at the rows on screen', () => {
  const { sizes, targets } = rig();
  const images = Array.from({ length: 1000 }, () => image());
  let shown = [];
  for (let pass = 0; pass < 20; pass++) {
    for (const li of shown) li.isConnected = false;
    const first = (pass * 137) % 950;
    shown = images.slice(first, first + 50).map((im) => {
      const li = row();
      sizes.watch(li, im);
      return li;
    });
    sizes.sweep();
  }
  assert.equal(targets.size, 50);
  assert.equal(sizes.size, 50);
});

test('a row nearing the viewport is measured once and released; its rebuild does not ask again', () => {
  const { sizes, targets, probes, intersect } = rig();
  const im = image();
  const li = row();
  sizes.watch(li, im);
  intersect(li);
  assert.equal(probes.length, 1);
  assert.equal(targets.size, 0, 'a measured row is no longer observed');
  sizes.watch(row(), im);
  assert.equal(targets.size, 0, 'the in-flight image is not watched again');
  probes[0].done();
  const retry = row();
  sizes.watch(retry, im);
  assert.equal(targets.size, 1, 'a probe that failed leaves the image free to be asked again');
});

test('a measured image is never watched, and a leaving ghost is forgotten at once', () => {
  const { sizes, targets } = rig();
  sizes.watch(row(), image(true));
  assert.equal(targets.size, 0);
  const ghost = row();
  sizes.watch(ghost, image());
  sizes.forget(ghost);
  assert.equal(targets.size, 0);
  assert.equal(sizes.size, 0);
});
