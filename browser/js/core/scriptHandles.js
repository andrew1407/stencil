// The wasm side of the .stc engine (core/wasmScriptApi.cpp): parse into an opaque handle,
// read the whole program out, destroy it. Everything the handle returns dies with it, so
// each read is copied into a plain object before scriptDestroy.
import { OP_KINDS, SOURCE_KINDS, TOKEN_KINDS } from './script/types.js';

const I32 = 4;
const nums = (n) => Array.from({ length: n }, () => 'number');

export const scriptExports = [
  'stencil_scriptParse', 'stencil_scriptDestroy', 'stencil_scriptErrorCount',
  'stencil_scriptDiagCount', 'stencil_scriptDiagAt', 'stencil_scriptTokenCount',
  'stencil_scriptTokenAt', 'stencil_scriptBlockCount', 'stencil_scriptBlockAt',
  'stencil_scriptOpCount', 'stencil_scriptOpAt', 'stencil_scriptOpStr',
  'stencil_scriptOpNum', 'stencil_scriptOpResolve', 'stencil_scriptDump',
  'stencil_scriptOpTokCount', 'stencil_scriptOpTok',
];

export const buildScriptOps = (core, { withCString }) => {
  const c = {
    parse: (ptr, len) => core.ccall('stencil_scriptParse', 'number', ['number', 'number'], [ptr, len]),
    destroy: core.cwrap('stencil_scriptDestroy', null, ['number']),
    errorCount: core.cwrap('stencil_scriptErrorCount', 'number', ['number']),
    diagCount: core.cwrap('stencil_scriptDiagCount', 'number', ['number']),
    diagAt: core.cwrap('stencil_scriptDiagAt', 'string', nums(7)),
    tokenCount: core.cwrap('stencil_scriptTokenCount', 'number', ['number']),
    tokenAt: core.cwrap('stencil_scriptTokenAt', 'number', nums(6)),
    blockCount: core.cwrap('stencil_scriptBlockCount', 'number', ['number']),
    blockAt: core.cwrap('stencil_scriptBlockAt', 'string', nums(6)),
    opCount: core.cwrap('stencil_scriptOpCount', 'number', ['number']),
    opAt: core.cwrap('stencil_scriptOpAt', 'number', nums(9)),
    opStr: core.cwrap('stencil_scriptOpStr', 'string', ['number', 'number', 'number']),
    opNum: core.cwrap('stencil_scriptOpNum', 'number', nums(4)),
    opTokCount: core.cwrap('stencil_scriptOpTokCount', 'number', ['number', 'number']),
    opTok: core.cwrap('stencil_scriptOpTok', 'string', ['number', 'number', 'number']),
    dump: core.cwrap('stencil_scriptDump', 'string', ['number']),
  };

  // ONE scratch slot for every out-parameter, taken once: a _malloc/_free pair per token churned
  // the heap on every keystroke. The widest call writes 7 ints; a double fits in the first two.
  const out = core._malloc(7 * I32);
  const slot = (i) => out + i * I32;
  const readInt = (i) => core.getValue(out + i * I32, 'i32');

  // UTF8ToString is not in EXPORTED_RUNTIME_METHODS, so read the NUL-terminated bytes
  // straight out of the heap. HEAPU8 is re-read per call: memory growth detaches it.
  const utf8 = new TextDecoder();
  const cstr = (ptr) => {
    if (!ptr) return '';
    const heap = core.HEAPU8;
    let end = ptr;
    while (heap[end] !== 0) end += 1;
    return utf8.decode(heap.subarray(ptr, end));
  };

  const readDiagnostics = (h, n) => {
    const list = [];
    for (let i = 0; i < n; i += 1) {
      const msg = c.diagAt(h, i, slot(0), slot(1), slot(2), slot(3), slot(4));
      list.push({
        severity: readInt(0) === 0 ? 'error' : 'warning',
        line: readInt(1),
        col: readInt(2),
        len: readInt(3),
        // The 5th slot holds a char** to the code string, itself handle-owned.
        code: cstr(readInt(4)),
        message: msg ?? '',
      });
    }
    return list;
  };

  const readTokens = (h, n) => {
    const list = [];
    for (let i = 0; i < n; i += 1) {
      c.tokenAt(h, i, slot(0), slot(1), slot(2), slot(3));
      list.push({
        kind: TOKEN_KINDS[readInt(0)] ?? 'ident',
        line: readInt(1),
        col: readInt(2),
        len: readInt(3),
        text: '',   // the span is what an editor paints by; only the fallback fills the lexeme
      });
    }
    return list;
  };

  const readBlocks = (h, n) => {
    const list = [];
    for (let i = 0; i < n; i += 1) {
      const source = c.blockAt(h, i, slot(0), slot(1), slot(2), slot(3));
      list.push({
        source: source ?? '',
        kind: SOURCE_KINDS[readInt(0)] ?? 'project',
        frame: readInt(1),
        opStart: readInt(2),
        opCount: readInt(3),
        line: 1,
      });
    }
    return list;
  };

  const readOps = (h, n) => {
    const list = [];
    for (let i = 0; i < n; i += 1) {
      c.opAt(h, i, slot(0), slot(1), slot(2), slot(3), slot(4), slot(5), slot(6));
      const kind = OP_KINDS[readInt(0)] ?? 'crop';
      const block = readInt(1);
      const editIndex = readInt(2);
      const line = readInt(3);
      const col = readInt(4);
      const strCount = readInt(5);
      const numCount = readInt(6);
      const strs = Array.from({ length: strCount }, (_, k) => c.opStr(h, i, k) ?? '');
      const opNums = Array.from({ length: numCount }, (_, k) => {
        c.opNum(h, i, k, out);
        return core.getValue(out, 'double');
      });
      const tokCount = Math.max(0, c.opTokCount(h, i));
      const toks = Array.from({ length: tokCount }, (_, k) => c.opTok(h, i, k) ?? '');
      list.push({ kind, block, editIndex, line, col, len: 0, strs, nums: opNums, toks });
    }
    return list;
  };

  return {
    scriptParse: (text) => withCString(String(text ?? ''), (ptr, len) => {
      const h = c.parse(ptr, len);
      if (!h) return { tokens: [], diagnostics: [], blocks: [], ops: [], errorCount: 0, hasErrors: false };
      try {
        const diagnostics = readDiagnostics(h, Math.max(0, c.diagCount(h)));
        return {
          tokens: readTokens(h, Math.max(0, c.tokenCount(h))),
          diagnostics,
          blocks: readBlocks(h, Math.max(0, c.blockCount(h))),
          ops: readOps(h, Math.max(0, c.opCount(h))),
          dump: c.dump(h) ?? '',
          errorCount: Math.max(0, c.errorCount(h)),
          hasErrors: c.errorCount(h) > 0,
        };
      } finally {
        c.destroy(h);
      }
    }),
  };
};
