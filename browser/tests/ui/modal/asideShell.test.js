// An `aside` shell (the console window): opening it leaves every other window up, opening another
// leaves it up, a press on its empty overlay closes nothing, and closeOpenModal spares it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement, installDom } from '../../helpers/dom.js';

const el = (id = '') => createStubElement('div', { id, getBoundingClientRect: () => ({ left: 0, top: 0, width: 10, height: 10, bottom: 10 }) });
installDom({}, { window: { matchMedia: () => ({ matches: true }), innerWidth: 1000, innerHeight: 800, addEventListener() {}, removeEventListener() {} } });
const { wireModalShell, closeOpenModal } = await import('../../../js/ui/base.js');
const { setMotionPrefs } = await import('../../../js/ui/motion/motionPrefs.js');

test('aside: beside the other windows, one at a time or not', () => {
  setMotionPrefs({ multiWindow: false });
  const other = wireModalShell(el('as-other'), null, null);
  const aside = wireModalShell(el('as-console'), null, null, { aside: true });
  other.open();
  aside.open();
  assert.deepEqual([other.isOpen(), aside.isOpen()], [true, true], 'the aside closes nothing on open');
  other.close();
  other.open();
  assert.deepEqual([other.isOpen(), aside.isOpen()], [true, true], 'and another open leaves it up');
  aside.overlay = undefined;
  closeOpenModal();
  assert.deepEqual([other.isOpen(), aside.isOpen()], [false, true], 'closeOpenModal spares it');
  aside.close();
  assert.equal(aside.isOpen(), false);
  assert.equal(aside.aside, true);
});
