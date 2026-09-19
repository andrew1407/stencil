// Shared `fetch` stub for the browser test suites: a fetch that answers from a response SPEC, or a queue of
// them, and records every call on `stub.calls` (each entry both `[url, options]` and an object). A spec is a
// plain object of VALUES ({ ok?, status?, json?, text?, blob?, arrayBuffer? }, ok defaulting true, text falling
// back to JSON.stringify(json)), an Error the fetch REJECTS with, a ready-made Response-ish passed through, or
// a function (url, options) => Promise<Response-ish>.

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
