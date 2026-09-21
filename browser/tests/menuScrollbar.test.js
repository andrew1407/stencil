// The drawn menu thumb (js/ui/menuScrollbar.js): it exists only when the list overflows its
// cap, and its length and travel follow the scroll.
import test from 'node:test';
import assert from 'node:assert/strict';
import { syncMenuScrollbar, attachMenuScrollbar } from '../js/ui/control/menuScrollbar.js';

const menuOf = (clientHeight, scrollHeight, scrollTop = 0) => {
  const props = new Map();
  const listeners = new Map();
  return {
    clientHeight, scrollHeight, scrollTop,
    style: {
      setProperty: (k, v) => props.set(k, v),
      removeProperty: (k) => props.delete(k),
      getPropertyValue: (k) => props.get(k) ?? '',
    },
    addEventListener: (t, fn) => listeners.set(t, fn),
    removeEventListener: (t) => listeners.delete(t),
    fire: (t) => listeners.get(t)?.(),
    has: (t) => listeners.has(t),
    props,
  };
};

test('a list that fits its cap draws no thumb at all', () => {
  const m = menuOf(300, 240);
  assert.equal(syncMenuScrollbar(m), null);
  assert.equal(m.props.has('--dd-sb-len'), false);
  assert.equal(m.props.has('--dd-sb-pos'), false);
});

test('an overflowing list gets a thumb shorter than its track', () => {
  const m = menuOf(316, 496);
  const t = syncMenuScrollbar(m);
  assert.ok(t.len > 0 && t.len < 316 - 6, 'the thumb is a fraction of the track');
  assert.equal(m.props.get('--dd-sb-pos'), '3px', 'parked at the top at scrollTop 0');
});

test('the thumb travels to the end of its track and no further', () => {
  const m = menuOf(316, 496);
  const track = 316 - 6;
  syncMenuScrollbar(m);
  const len = parseFloat(m.props.get('--dd-sb-len'));
  m.scrollTop = m.scrollHeight - m.clientHeight;
  syncMenuScrollbar(m);
  assert.equal(parseFloat(m.props.get('--dd-sb-pos')), 3 + (track - len));
});

test('attach follows the scroll and detach leaves nothing behind', () => {
  const m = menuOf(316, 496);
  const off = attachMenuScrollbar(m);
  assert.ok(m.has('scroll'), 'it listens while open');
  m.scrollTop = 90;
  m.fire('scroll');
  assert.ok(parseFloat(m.props.get('--dd-sb-pos')) > 3, 'the thumb moved with the content');
  off();
  assert.equal(m.has('scroll'), false);
  assert.equal(m.props.has('--dd-sb-len'), false, 'and the layer is cleared');
});
