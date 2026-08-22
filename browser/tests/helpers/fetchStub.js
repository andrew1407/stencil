// Shared `fetch` stub for the browser test suites.
//
// Node's real fetch would hit the network, so the suites that drive fetch-using code
// each grew their own inline stand-in — a fixed `{ ok, json }` object here, a
// call-recording wrapper there. This module is those stubs unified: a fetch function
// that answers from a response SPEC (or a queue of them), and records every call.
//
// A response spec is any of:
//   - a plain object of VALUES: { ok?, status?, json?, text?, blob?, arrayBuffer? }
//     — wrapped into a Response-ish whose json()/text()/blob()/arrayBuffer() resolve
//     to those values (ok defaults true; text falls back to JSON.stringify(json));
//   - an Error — the fetch REJECTS with it (the network-failure path);
//   - a ready-made Response-ish (json/text/blob already functions) — passed through;
//   - a function (url, options) => Promise<Response-ish> — a full custom impl.
//
// Calls are recorded on `stub.calls`, each entry both an array `[url, options]` (for
// suites that destructure the raw arguments) and an object with `.url`/`.options`.

const isResponseLike = (s) =>
  ['json', 'text', 'blob', 'arrayBuffer'].some((k) => typeof s?.[k] === 'function');

const toResponse = (spec) => {
  if (spec instanceof Error) return Promise.reject(spec);
  if (isResponseLike(spec)) return Promise.resolve(spec);
  const { ok = true, status = ok ? 200 : 500, json, text, blob, arrayBuffer } = spec ?? {};
  return Promise.resolve({
    ok, status,
    json: async () => json,
    text: async () => text ?? (json !== undefined ? JSON.stringify(json) : ''),
    blob: async () => blob,
    arrayBuffer: async () => arrayBuffer,
  });
};

/**
 * A fetch stand-in. Answers from the one-shot `queue(...)` first, then from
 * `respond` — which is a plain writable property, so a test can swap it partway
 * through (the reason it is not captured in a closure).
 * @param {object|Error|Function} respond - The default response spec (see above).
 */
export const createFetchStub = (respond = {}) => {
  const queue = [];
  const stub = (url, options) => {
    stub.calls.push(Object.assign([url, options], { url, options }));
    const next = queue.length ? queue.shift() : stub.respond;
    return typeof next === 'function'
      ? Promise.resolve().then(() => next(url, options))
      : toResponse(next);
  };
  stub.calls = [];
  stub.respond = respond;
  stub.queue = (...specs) => { queue.push(...specs); return stub; };
  stub.reset = () => { stub.calls.length = 0; queue.length = 0; };
  return stub;
};

/**
 * Install a fetch stub as globalThis.fetch.
 * @returns the stub, with an extra `restore()` that puts the previous fetch back.
 */
export const installFetchStub = (respond = {}) => {
  const stub = createFetchStub(respond);
  const prev = Object.getOwnPropertyDescriptor(globalThis, 'fetch');
  globalThis.fetch = stub;
  stub.restore = () => {
    if (prev) Object.defineProperty(globalThis, 'fetch', prev);
    else delete globalThis.fetch;
  };
  return stub;
};
