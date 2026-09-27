// The app-level voice chat, installed the way the app installs it (installVoiceModes): the
// real logged-turn path over a stub controller, the balloon behind a stub notify element,
// and a spied app.chat. One spoken utterance, sent by its phrase, is one turn.
import { installDom, createStubElement } from './dom.js';
import { fakeEngine, fakeWindow } from './voiceModesRig.js';
import { stubClock } from './speech.js';
import { installVoiceModes } from '../../js/llm/voice/modes.js';
import { sharedChatController, forgetChatController, resetChatLog } from '../../js/llm/chat/session.js';

export const installVoiceRig = ({ entry = { reply: 'Done.', results: [] }, chatOpen = false } = {}) => {
  const toasts = [];
  const chatCalls = [];
  const doc = installDom();
  doc.register('notify-balloon', createStubElement('div', {
    notify: (msg, type, opts) => toasts.push({ msg, type, opts }),
  }));
  const app = {
    chat: {
      isOpen: () => chatOpen,
      open: () => chatCalls.push('open'),
      markUnread: () => chatCalls.push('markUnread'),
    },
  };
  const sent = [];
  sharedChatController(app, { create: () => ({ attachments: [], send: async (text) => { sent.push(text); return entry; } }) });
  const engine = fakeEngine();
  const clock = stubClock();
  const voice = installVoiceModes(app, {
    engine, win: fakeWindow(), setTimer: clock.setTimer, clearTimer: clock.clearTimer, now: clock.now,
    loadSettings: () => ({ silenceMs: 1000, language: 'default' }),
  });
  voice.voiceChat = true;
  const say = async (words) => {
    engine.hear(`${words} send`);
    for (let i = 0; i < 5; i++) await new Promise((r) => setTimeout(r, 0));
  };
  const restore = () => { voice.dispose(); forgetChatController(app); resetChatLog(); doc.restore(); };
  return { app, voice, toasts, chatCalls, sent, say, restore };
};
