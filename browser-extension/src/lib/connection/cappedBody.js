// Extension copy of browser/js/net/cappedBody.js: a response body read under a byte cap, declared
// or streamed — the one bounded read page-derived bytes, a server's and a provider's reply go
// through. Byte-pinned (portParity).

// 64 MiB, the cap the CLI (net.MAX_FETCH_BYTES), desktop, pystencil and the bot read up to.
export const MAX_FETCH_BYTES = 64 * 1024 * 1024;
// An error body only ever yields its `message` (pystencil _MAX_ERROR_BYTES).
export const MAX_ERROR_BYTES = 64 * 1024;

export const cancelBody = async (resp) => { try { await resp.body?.cancel(); } catch { /* already closed */ } };

// The body as an ArrayBuffer, refused once it passes `max` bytes, declared or streamed.
export const readCapped = async (resp, max = MAX_FETCH_BYTES) => {
  const tooBig = () => new Error(`response exceeds the ${max}-byte fetch cap`);
  if (Number(resp.headers?.get?.('content-length') || 0) > max) {
    await cancelBody(resp);
    throw tooBig();
  }
  const reader = typeof resp.body?.getReader === 'function' ? resp.body.getReader() : null;
  if (!reader) {
    const buf = await resp.arrayBuffer();
    if (buf.byteLength > max) throw tooBig();
    return buf;
  }
  const chunks = [];
  let total = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > max) {
      try { await reader.cancel(); } catch { /* already closed */ }
      throw tooBig();
    }
    chunks.push(value);
  }
  const out = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) { out.set(c, at); at += c.byteLength; }
  return out.buffer;
};

// The body as a Blob of the response's own type under the same cap; a response with no stream
// (an empty body, an injected fetch's plain object) reads through its own blob(), measured after.
export const readBlobCapped = async (resp, max = MAX_FETCH_BYTES) => {
  if (typeof resp.body?.getReader !== 'function' && typeof resp.blob === 'function') {
    const blob = await resp.blob();
    if (blob.size > max) throw new Error(`response exceeds the ${max}-byte fetch cap`);
    return blob;
  }
  const type = resp.headers?.get?.('content-type') || '';
  return new Blob([await readCapped(resp, max)], { type });
};

// The body parsed as JSON under the same cap. A response with neither a stream nor a buffer
// (an injected fetch's plain object) reads through its own json().
export const readJsonCapped = async (resp, max = MAX_FETCH_BYTES) => {
  if (typeof resp.body?.getReader !== 'function' && typeof resp.arrayBuffer !== 'function') return resp.json();
  return JSON.parse(new TextDecoder().decode(await readCapped(resp, max)));
};
