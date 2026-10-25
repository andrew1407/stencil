// The offscreen document the service worker writes the clipboard through (it has no DOM of its
// own): a copy request puts its text in the textarea and copies it. Imports nothing, like the
// classic content scripts, so its channel is a mirror of lib/messages.js.
const MSG = { OFFSCREEN_COPY: 'stencil-offscreen-copy' };

chrome.runtime.onMessage.addListener((msg, _sender, reply) => {
  if (msg?.type !== MSG.OFFSCREEN_COPY) return false;
  const area = document.getElementById('text');
  area.value = String(msg.text ?? '');
  area.select();
  reply({ ok: document.execCommand('copy') });
  return false;
});
