// A turn no reply answered leaves no trace in the replayed history: the stopped or failed user
// turn is dropped, so a Retry or the next message never replays two user turns in a row.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createChatController } from '../../src/llm/chatController.js';

// The first call waits on its signal and rejects as fetch does on abort; later calls answer.
const abortableClient = () => {
  const calls = [];
  return {
    calls,
    client: {
      chat: ({ messages, signal }) => {
        calls.push(messages);
        if (calls.length > 1) return Promise.resolve('fine');
        return new Promise((_, reject) => signal.addEventListener('abort', () => {
          const err = new Error('aborted'); err.name = 'AbortError'; reject(err);
        }));
      },
    },
  };
};

test('a stopped turn is dropped, and the retry replays one user turn', async () => {
  const { client, calls } = abortableClient();
  const controller = createChatController({ getClient: () => client, getListing: () => [] });
  const stop = new AbortController();
  const turn = controller.send('first', { signal: stop.signal });
  await new Promise((r) => setImmediate(r));
  assert.deepEqual(controller.history.map((m) => m.role), ['user']);
  stop.abort();
  await assert.rejects(turn, { name: 'AbortError' });
  assert.deepEqual(controller.history, []);

  await controller.send('first', { signal: new AbortController().signal });
  assert.deepEqual(calls[1].map((m) => [m.role, m.text]), [['user', 'first']]);
  assert.deepEqual(controller.history.map((m) => m.role), ['user', 'assistant']);
});

test('a failure after the reply landed keeps the answered pair', async () => {
  const client = { chat: async () => '{"version":1,"reply":"x","actions":[{"op":"nope"}],"variants":[]}' };
  const controller = createChatController({ getClient: () => client, getListing: () => [] });
  await controller.send('hi').catch(() => {});
  assert.deepEqual(controller.history.map((m) => m.role), ['user', 'assistant']);
});
