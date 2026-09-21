// Shared rig for the chatController.test.js family: the client double that captures what it
// was sent, the recording stencil facade, and a controller wired to both.
import { createChatController } from '../../js/llm/chat/controller.js';

// ── Doubles: client capturing what it was sent, stub facade recording calls ──
export const makeClient = (replies) => {
  const calls = [];
  const queue = Array.isArray(replies) ? replies.slice() : [replies];
  return {
    calls,
    chat: async ({ system, messages }) => {
      calls.push({ system, messages });
      return queue.length > 1 ? queue.shift() : queue[0];
    },
  };
};

export const makeStencil = () => {
  const calls = [];
  const stencil = {
    imageSize: { width: 800, height: 600 },
    connections: [],
    crop(spec) { calls.push(['crop', spec]); return stencil; },
    rotateLeft() { calls.push(['rotateLeft']); return stencil; },
    rotateRight() { calls.push(['rotateRight']); return stencil; },
    apply(opts) { calls.push(['apply', opts]); return stencil; },
    async blank(color) { calls.push(['blank', color]); return stencil; },
    async load(url, opts) { calls.push(['load', url, opts]); return stencil; },
    async connect(entry) { calls.push(['connect', entry]); return stencil; },
    disconnect(url) { calls.push(['disconnect', url]); return stencil; },
    undo() { calls.push(['undo']); return stencil; },
    redo() { calls.push(['redo']); return stencil; },
    zoomFit() { calls.push(['zoomFit']); return stencil; },
  };
  Object.defineProperty(stencil, 'darkTheme', { set(v) { calls.push(['darkTheme', v]); } });
  Object.defineProperty(stencil, 'mainTheme', { set(v) { calls.push(['mainTheme', v]); } });
  Object.defineProperty(stencil, 'zoomLevel', { set(v) { calls.push(['zoomLevel', v]); } });
  Object.defineProperty(stencil, 'compareMode', { set(v) { calls.push(['compareMode', v]); } });
  Object.defineProperty(stencil, 'compareSplit', { set(v) { calls.push(['compareSplit', v]); } });
  // The facade's incognito rule: only togglable on a blank editor — throws otherwise.
  Object.defineProperty(stencil, 'incognito', { set(v) {
    if (v && stencil.imageSize) throw new Error('Incognito can only be enabled on a blank editor (before an image is loaded)');
    calls.push(['incognitoSet', v]);
  } });
  return { stencil, calls };
};

export const chatOnlyReply = 'Just chatting, no JSON.';
export const variantPlan = JSON.stringify({
  version: 1,
  reply: 'Two takes for you.',
  actions: [{ op: 'rotate', dir: 'left' }],
  variants: [
    { label: 'sepia', actions: [{ op: 'filter', mode: 'sepia' }] },
    { label: 'cropped', actions: [{ op: 'crop', spec: { x1: '10%' } }] },
  ],
});

export const pngUrl = (data) => `data:image/png;base64,${data}`;
export const stubFile = (name, type) => ({ name, type });

export const makeController = (client, over = {}) => {
  const { stencil, calls } = makeStencil();
  let exports = 0;
  const exported = [];
  const controller = createChatController({
    stencil,
    getClient: () => client,
    exportImage: async () => { const u = pngUrl(`EXP${exports++}`); exported.push(u); return u; },
    prepareAttachment: async (file) => pngUrl(`IMG_${file.name}`),
    extractFrames: async (file, n) => Array.from({ length: n }, (_, i) => pngUrl(`FR${i}_${file.name}`)),
    frameAt: async (file, i) => pngUrl(`AT${i}_${file.name}`),
    // Contract §7: every turn carries a snapshot of the working image. Injected here
    // so it is distinguishable from the variant exports (and needs no DOM downscale).
    workingSnapshot: async () => pngUrl('SHOT'),
    ...over,
  });
  return { controller, stencil, calls, exported };
};
