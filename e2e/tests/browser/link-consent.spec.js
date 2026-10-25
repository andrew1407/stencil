// A launch link on the real app (contracts/launch/launch-uri.md §2) cannot re-point the assistant
// without a confirm — declined, the settings and the stored key are as they were, and no request
// reaches the link's endpoint — and cannot replace a saved server credential.
import http from 'node:http';
import { test, expect } from '@playwright/test';
import { APP_URL } from '../../helpers/config.js';

const EVIL = 'https://llm.evil.example';

// gotoApp clears storage on every load, so these specs seed it once, then open the link themselves.
const gotoSeeded = async (page, seed, hash) => {
  await page.addInitScript((entries) => {
    if (sessionStorage.getItem('seeded')) return;
    sessionStorage.setItem('seeded', '1');
    localStorage.clear();
    for (const [k, v] of Object.entries(entries)) localStorage.setItem(k, v);
  }, seed);
  await page.goto(APP_URL + hash);
  await page.waitForFunction(() => !!window.stencil, null, { timeout: 15_000 });
};
const ASSISTANT = { drawingApp_llmSettings: JSON.stringify({
  provider: 'openai-compat', baseUrl: 'http://127.0.0.1:9/v1', model: 'm', apiKey: 'sk-e2e-mine', serverUrl: '',
}) };

test('a declined assistant link changes nothing and never reaches the endpoint it names', async ({ page }) => {
  const toEvil = [];
  page.on('request', (req) => { if (req.url().startsWith(EVIL)) toEvil.push(req.url()); });
  await gotoSeeded(page, ASSISTANT, `#llm-base-url=${encodeURIComponent(EVIL)}`);
  await expect(page.locator('#confirm-modal-message')).toContainText(EVIL, { timeout: 15_000 });
  await page.locator('#confirm-modal-cancel').click();
  await page.waitForFunction(() => location.hash === '', null, { timeout: 15_000 });
  await page.waitForTimeout(500);
  const state = await page.evaluate(() => ({ baseUrl: window.stencil.llm.baseUrl, key: window.stencil.llm.apiKey }));
  expect(state).toEqual({ baseUrl: 'http://127.0.0.1:9/v1', key: 'sk-e2e-mine' });
  expect(toEvil).toEqual([]);
});

test('an accepted assistant link moves the endpoint but never carries the stored key', async ({ page }) => {
  await gotoSeeded(page, ASSISTANT, `#llm-base-url=${encodeURIComponent(EVIL)}`);
  await page.locator('#confirm-modal-confirm').click({ timeout: 15_000 });
  await page.waitForFunction(() => window.stencil?.llm?.baseUrl?.includes('evil'), null, { timeout: 15_000 });
  const state = await page.evaluate(() => ({
    key: window.stencil.llm.apiKey, stored: localStorage.getItem('drawingApp_llmSettings') || '',
  }));
  expect(state.key).toBe('');
  expect(state.stored.includes('sk-e2e-mine')).toBe(false);
});

// A stand-in collaboration server that accepts only `good` as a bearer, answering CORS for the app.
const acceptingServer = async (good) => {
  const server = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', req.headers.origin || '*');
    res.setHeader('Access-Control-Allow-Headers', 'Authorization, Content-Type, X-Admin-Token, If-None-Match');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }
    const ok = req.headers.authorization === `Bearer ${good}`;
    res.writeHead(ok ? 200 : 401, { 'Content-Type': 'application/json' });
    res.end(ok ? '{"projects":[]}' : '{"code":"unauthorized","message":"no"}');
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  return { server, url: `http://127.0.0.1:${server.address().port}` };
};

test('a link token for a saved server connects for the session and never replaces the saved credential', async ({ page }) => {
  const { server, url } = await acceptingServer('tok-theirs');
  try {
    await gotoSeeded(page, {
      drawingApp_servers: JSON.stringify([{ url, token: 'tok-mine' }]),
      drawingApp_autoConnectServers: '0',
    }, `#connect=${encodeURIComponent(url)}&token=tok-theirs`);
    await page.waitForFunction((u) => window.stencil.servers?.some?.((s) => s.url === u && s.connected)
      || (location.hash === '' && JSON.parse(localStorage.getItem('drawingApp_servers') || '[]').length > 0), url, { timeout: 15_000 });
    await page.waitForTimeout(300);
    const saved = await page.evaluate(() => JSON.parse(localStorage.getItem('drawingApp_servers') || '[]'));
    expect(saved).toEqual([{ url, token: 'tok-mine' }]);
  } finally {
    server.close();
  }
});
