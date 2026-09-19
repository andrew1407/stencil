// Scripted-client harness shared by the llmChat*.test.js suites: a queued mock client
// plus stub focus/open/attach capabilities (no chrome, no fetch).
import { formatOfItem } from '../../src/lib/filters.js';
import { createChatController } from '../../src/llm/chatController.js';

export const LISTING = [
  { kind: 'img', src: 'https://a.example/one.png', w: 10, h: 10, alt: '', name: 'one.png' },
  { kind: 'img', src: 'https://a.example/two.png', w: 20, h: 20, alt: 'second', name: 'two.png' },
  { kind: 'img', src: 'https://a.example/three.png', w: 30, h: 30, alt: '', name: 'three.png' },
];

// A scripted client: replies from the queue (last reply repeats), records calls.
export const scriptedClient = (responses) => {
  const calls = [];
  const client = {
    chat: async ({ system, messages }) => {
      calls.push({ system, messages: JSON.parse(JSON.stringify(messages)) });
      return responses[Math.min(calls.length - 1, responses.length - 1)];
    },
  };
  return { calls, client };
};

export const makeController = (responses, overrides = {}) => {
  const { calls, client } = scriptedClient(responses);
  const log = { focused: [], opened: [], attached: [] };
  const controller = createChatController({
    getClient: () => client,
    getListing: () => LISTING,
    formatOfItem,
    pageUrl: () => 'https://a.example/page',
    focusImage: async (i) => { log.focused.push(i); return true; },
    openImage: async (a) => { log.opened.push(a); return []; },
    attachImage: async (i) => { log.attached.push(i); return { mediaType: 'image/png', data: `IMG${i}` }; },
    ...overrides,
  });
  return { controller, calls, log };
};

export const ATTACH_PLAN = JSON.stringify({ version: 1, reply: 'Let me look at them.', actions: [{ op: 'attach', images: [0, 2] }], variants: [] });
