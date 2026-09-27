// A server's global project feed: /ws opened with a hello that names no project streams one
// `project-event` per create, update and delete (server/internal/protocol ws.go). The token rides
// the hello frame, never the URL. The twin of the browser client's #openEvents.
export const eventsUrl = (serverUrl) => String(serverUrl).replace(/^http/i, 'ws') + '/ws';

// onState(true) once the hello is sent, onState(false) when the server drops the socket; close()
// ends it without a report. Null when there is no WebSocket or the URL is refused.
export const subscribeProjectEvents = (conn, onEvent, { WebSocket: WS = globalThis.WebSocket, onState = () => {} } = {}) => {
  if (typeof WS !== 'function' || !conn || !conn.url) return null;
  let ws;
  try { ws = new WS(eventsUrl(conn.url)); } catch { return null; }
  let closed = false;
  ws.addEventListener('open', () => {
    try { ws.send(JSON.stringify({ type: 'hello', token: conn.token || '' })); } catch { return; }
    onState(true);
  });
  ws.addEventListener('message', (ev) => {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg && msg.type === 'project-event') onEvent(msg);
  });
  ws.addEventListener('close', () => { if (!closed) { closed = true; onState(false); } });
  return {
    close: () => {
      closed = true;
      try { ws.close(); } catch { /* already closed */ }
    },
  };
};
