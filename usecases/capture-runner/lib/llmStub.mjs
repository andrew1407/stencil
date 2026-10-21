// The canned assistant: e2e's LLM stub, fed the op-plans config/shared.json declares. Every
// plan is a valid contract §1 envelope, so the app really executes it on the canvas.
import http from 'node:http';
import { e2e } from './playwright.mjs';

export const startLlmStub = async () => (await e2e('helpers/llm-stub.js')).startLlmStub();

export const chatOnlyPlan = (reply) => ({ version: 1, reply, actions: [], variants: [] });

// Ollama's version endpoint on loopback, so the settings window's probe finds a local model
// server; when a real Ollama holds the port, it answers instead.
export const startOllamaFace = (port, version) => new Promise((resolve) => {
  const server = http.createServer((req, res) => {
    const head = { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Headers': '*' };
    if (req.method === 'OPTIONS') { res.writeHead(204, head).end(); return; }
    if (req.url === '/api/version') {
      res.writeHead(200, { ...head, 'Content-Type': 'application/json' }).end(JSON.stringify({ version }));
      return;
    }
    res.writeHead(404, head).end();
  });
  server.once('error', () => resolve({ url: `http://127.0.0.1:${port}`, stop: () => {} }));
  server.listen(port, '127.0.0.1', () => resolve({ url: `http://127.0.0.1:${port}`, stop: () => server.close() }));
});
