// The wasm side of the .stc engine (core/wasmScriptApi.cpp): parse into an opaque handle,
// read the whole program out, destroy it. Everything the handle returns dies with it, so
// each read is copied into a plain object before scriptDestroy.
import { OP_KINDS, SOURCE_KINDS, TOKEN_KINDS } from './scriptTypes.js';

const I32 = 4;

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
    tokenCount: core.cwrap('stencil_scriptTokenCount', 'number', ['number']),
    blockCount: core.cwrap('stencil_scriptBlockCount', 'number', ['number']),
    opCount: core.cwrap('stencil_scriptOpCount', 'number', ['number']),
    opStr: core.cwrap('stencil_scriptOpStr', 'string', ['number', 'number', 'number']),
    opTokCount: core.cwrap('stencil_scriptOpTokCount', 'number', ['number', 'number']),
    opTok: core.cwrap('stencil_scriptOpTok', 'string', ['number', 'number', 'number']),
    dump: core.cwrap('stencil_scriptDump', 'string', ['number']),
  };

  // A scratch slot for the out-parameters; the widest call writes 7 ints.
  const withInts = (count, fn) => {
    const ptr = core._malloc(count * I32);
    try {
      return fn(ptr);
    } finally {
      core._free(ptr);
    }
  };
  const readInt = (ptr, i) => core.getValue(ptr + i * I32, 'i32');

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
    const out = [];
    for (let i = 0; i < n; i += 1) {
      const d = withInts(5, (p) => {
        const msg = core.ccall('stencil_scriptDiagAt', 'string',
          ['number', 'number', 'number', 'number', 'number', 'number', 'number'],
          [h, i, p, p + I32, p + 2 * I32, p + 3 * I32, p + 4 * I32]);
        // The 5th slot holds a char** to the code string, itself handle-owned.
        const codePtr = readInt(p, 4);
        return {
          severity: readInt(p, 0) === 0 ? 'error' : 'warning',
          line: readInt(p, 1),
          col: readInt(p, 2),
          len: readInt(p, 3),
          code: cstr(codePtr),
          message: msg ?? '',
        };
      });
      out.push(d);
    }
    return out;
  };

  const readTokens = (h, n) => {
    const out = [];
    for (let i = 0; i < n; i += 1) {
      out.push(withInts(4, (p) => {
        core.ccall('stencil_scriptTokenAt', 'number',
          ['number', 'number', 'number', 'number', 'number', 'number'],
          [h, i, p, p + I32, p + 2 * I32, p + 3 * I32]);
        return {
          kind: TOKEN_KINDS[readInt(p, 0)] ?? 'ident',
          line: readInt(p, 1),
          col: readInt(p, 2),
          len: readInt(p, 3),
          text: '',
        };
      }));
    }
    return out;
  };

  const readBlocks = (h, n) => {
    const out = [];
    for (let i = 0; i < n; i += 1) {
      out.push(withInts(4, (p) => {
        const source = core.ccall('stencil_scriptBlockAt', 'string',
          ['number', 'number', 'number', 'number', 'number', 'number'],
          [h, i, p, p + I32, p + 2 * I32, p + 3 * I32]);
        return {
          source: source ?? '',
          kind: SOURCE_KINDS[readInt(p, 0)] ?? 'project',
          frame: readInt(p, 1),
          opStart: readInt(p, 2),
          opCount: readInt(p, 3),
          line: 1,
        };
      }));
    }
    return out;
  };

  const readOps = (h, n) => {
    const out = [];
    for (let i = 0; i < n; i += 1) {
      const op = withInts(7, (p) => {
        core.ccall('stencil_scriptOpAt', 'number',
          ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
          [h, i, p, p + I32, p + 2 * I32, p + 3 * I32, p + 4 * I32, p + 5 * I32, p + 6 * I32]);
        return {
          kind: OP_KINDS[readInt(p, 0)] ?? 'crop',
          block: readInt(p, 1),
          editIndex: readInt(p, 2),
          line: readInt(p, 3),
          col: readInt(p, 4),
          len: 0,
          strCount: readInt(p, 5),
          numCount: readInt(p, 6),
        };
      });
      op.strs = Array.from({ length: op.strCount }, (_, k) => c.opStr(h, i, k) ?? '');
      op.nums = Array.from({ length: op.numCount }, (_, k) => withInts(2, (p) => {
        core.ccall('stencil_scriptOpNum', 'number', ['number', 'number', 'number', 'number'], [h, i, k, p]);
        return core.getValue(p, 'double');
      }));
      delete op.strCount;
      delete op.numCount;
      const tokCount = Math.max(0, c.opTokCount(h, i));
      op.toks = Array.from({ length: tokCount }, (_, k) => c.opTok(h, i, k) ?? '');
      out.push(op);
    }
    return out;
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
