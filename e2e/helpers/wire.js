// Live-edit protocol clients mirroring server/internal/protocol (WSMessage envelope)
// and the hello→subscribe→welcome→edit/save handshake from server/internal/hub/hub_test.go.
// WS uses the `ws` lib (one JSON text frame per message); raw TCP uses NDJSON (one
// compact-JSON record per line) over Node's built-in net.
import WebSocket from 'ws';
import net from 'node:net';
import { SERVER_WS, SERVER_TCP_PORT } from './serverApi.js';

// WS message type constants (protocol.go).
export const T = {
  hello: 'hello', subscribe: 'subscribe', edit: 'edit', save: 'save', ping: 'ping',
  cursor: 'cursor', presence: 'presence',
  welcome: 'welcome', peerJoin: 'peer-join', peerLeave: 'peer-leave', synced: 'synced',
  error: 'error', pong: 'pong', projectEvent: 'project-event',
};

// A tiny promise-based client with a shared read-until helper. `dial` opens the
// transport; `send` writes one JSON message; `readUntil` resolves the first frame of
// a wanted type (rejects on timeout). Works for both WS and TCP via a common shape.
class Client {
  constructor(send, close) { this._send = send; this._close = close; this._q = []; this._waiters = []; }
  _push(msg) {
    const w = this._waiters.findIndex((w) => w.type === msg.type && (w.match ? w.match(msg) : true));
    if (w >= 0) { const [{ resolve }] = this._waiters.splice(w, 1); resolve(msg); }
    else this._q.push(msg);
  }
  send(msg) { this._send(JSON.stringify(msg)); return this; }
  readUntil(type, timeoutMs = 4000) { return this.readWhere(type, () => true, timeoutMs); }
  // Like readUntil, but only resolves a frame of `type` that also satisfies `match`
  // (e.g. a peer-join for a SPECIFIC client — the server also emits a self peer-join).
  readWhere(type, match, timeoutMs = 4000) {
    const hit = this._q.findIndex((m) => m.type === type && match(m));
    if (hit >= 0) return Promise.resolve(this._q.splice(hit, 1)[0]);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const i = this._waiters.findIndex((w) => w.resolve === wrapped);
        if (i >= 0) this._waiters.splice(i, 1);
        reject(new Error(`timed out waiting for '${type}'`));
      }, timeoutMs);
      const wrapped = (m) => { clearTimeout(timer); resolve(m); };
      this._waiters.push({ type, match, resolve: wrapped });
    });
  }
  close() { this._close(); }
}

// Open a WebSocket client to /ws.
export function dialWS() {
  const ws = new WebSocket(SERVER_WS);
  const c = new Client((data) => ws.send(data), () => ws.close());
  c._raw = ws;                       // exposed so a test can abruptly terminate() a peer
  c.terminate = () => ws.terminate();
  ws.on('message', (data) => { try { c._push(JSON.parse(data.toString())); } catch { /* ignore */ } });
  return new Promise((resolve, reject) => {
    ws.on('open', () => resolve(c));
    ws.on('error', reject);
  });
}

// Open a raw-TCP NDJSON client.
export function dialTCP(port = SERVER_TCP_PORT) {
  const sock = net.connect(port, '127.0.0.1');
  const c = new Client((data) => sock.write(data + '\n'), () => sock.end());
  let buf = '';
  sock.on('data', (chunk) => {
    buf += chunk.toString();
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl); buf = buf.slice(nl + 1);
      if (line.trim()) { try { c._push(JSON.parse(line)); } catch { /* ignore */ } }
    }
  });
  return new Promise((resolve, reject) => {
    sock.on('connect', () => resolve(c));
    sock.on('error', reject);
  });
}

// Join a project session over a freshly dialed client: hello + subscribe, await welcome.
export async function join(client, { token, projectId, clientId }) {
  client.send({ type: T.hello, token, projectId, clientId });
  client.send({ type: T.subscribe });
  return client.readUntil(T.welcome);
}
