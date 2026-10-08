// The worker end of lib/prefs/writeChain.js: an extension page's pin or ledger write runs here,
// on the worker's one chain per key, so no two contexts interleave a read-modify-write.
import { pinWrites } from '../../lib/prefs/pins.js';
import { ledgerWrites } from '../../lib/prefs/ledger.js';
import { MSG } from '../../lib/messages.js';
import { answers, fromExtensionPage } from '../editorRelay.js';

const WRITES = Object.freeze({ pins: pinWrites, ledger: ledgerWrites });

const writeFor = (store, op) =>
  (Object.hasOwn(WRITES, store) && Object.hasOwn(WRITES[store], op)) ? WRITES[store][op] : null;

export const storeWriteHandlers = {
  [MSG.STORE_WRITE]: answers(async (msg, sender) => {
    if (!fromExtensionPage(sender)) return { ok: false, error: 'this request is not allowed from a page' };
    const write = writeFor(msg.store, msg.op);
    if (!write) return { ok: false, error: 'unknown store write' };
    return { ok: true, value: await write(...(Array.isArray(msg.args) ? msg.args : [])) };
  }),
};
