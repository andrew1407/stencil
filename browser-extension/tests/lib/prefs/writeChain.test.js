// The opened ledger's writes in one document: recordOpened and pruneLedger read inside the same
// chain, so a prune that starts while a record is in flight sees that record.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installChromeStub } from '../../helpers/chromeStub.js';
import { recordOpened, pruneLedger, loadLedger, LEDGER_KEY } from '../../../src/lib/prefs/ledger.js';
import { writeChain } from '../../../src/lib/prefs/writeChain.js';

const EDITOR = 'http://localhost:8080/';
const ORIGIN = 'http://localhost:8080';
const open = (n) => recordOpened({ source: `https://cdn/${n}.png`, name: `${n}.png`, editorUrl: EDITOR });

test('concurrent records all land', async () => {
  installChromeStub();
  await Promise.all([1, 2, 3, 4, 5].map(open));
  assert.equal((await loadLedger()).length, 5);
});

test('a prune racing a record keeps the fresh record and drops only the stale entry', async () => {
  installChromeStub({ local: { [LEDGER_KEY]: [
    { source: 'https://cdn/old.png', resource: '', name: 'old.png', editorUrl: EDITOR, t: 1, count: 1 },
  ] } });
  const [rec, pruned] = await Promise.all([open('new'), pruneLedger([], ORIGIN)]);
  assert.equal(rec.count, 1);
  assert.equal(pruned, true);
  assert.deepEqual((await loadLedger()).map((e) => e.source), ['https://cdn/new.png']);
});

test('a record after a prune is merged against the pruned list', async () => {
  installChromeStub({ local: { [LEDGER_KEY]: [
    { source: 'https://cdn/old.png', resource: '', name: 'old.png', editorUrl: EDITOR, t: 1, count: 1 },
  ] } });
  await Promise.all([pruneLedger([], ORIGIN), open('new'), open('new')]);
  const after = await loadLedger();
  assert.deepEqual(after.map((e) => [e.source, e.count]), [['https://cdn/new.png', 2]]);
});

test('a rejected turn does not wedge the chain', async () => {
  const turn = writeChain();
  const order = [];
  const failed = turn(async () => { order.push('a'); throw new Error('boom'); });
  const next = turn(async () => { order.push('b'); return 'ok'; });
  await assert.rejects(failed, /boom/);
  assert.equal(await next, 'ok');
  assert.deepEqual(order, ['a', 'b']);
});
