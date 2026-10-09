// The rig behind tests/llm/llmClient*.test.js: a fetch that records every request and answers from
// a queue (the last reply repeats), and the one conversation the client suites send.

export const makeMockFetch = (responses) => {
  const calls = [];
  const queue = Array.isArray(responses) ? responses.slice() : [responses];
  const fetchImpl = async (url, init = {}) => {
    calls.push({ url, init });
    const next = queue.length > 1 ? queue.shift() : queue[0];
    const { status = 200, body = {} } = next || {};
    return { ok: status >= 200 && status < 300, status, json: async () => body };
  };
  return { calls, fetchImpl };
};

// Two images on the first user turn: each provider's wire carries them its own way.
export const MSGS = [
  { role: 'user', text: 'hello', images: [{ mediaType: 'image/png', data: 'AAAA' }, { mediaType: 'image/jpeg', data: 'BBBB' }] },
  { role: 'assistant', text: 'prior reply' },
  { role: 'user', text: 'again' },
];
