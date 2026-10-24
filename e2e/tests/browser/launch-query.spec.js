// The launch query (contracts/launch/launch-uri.md) on the real app: the page URL's `?key=value`
// pairs open a blank or a served picture, set the look and the editor settings, keep a URL source
// as the project's source, and are stripped once consumed; an incognito launch saves nothing, and
// a credential is taken from the fragment, never from the query.
import { test, expect } from '@playwright/test';
import { gotoApp, SITE_URL } from '../../helpers/boot.js';

// Ready once the launch has landed a picture and stripped its pairs from the address bar.
const launched = (page) => page.waitForFunction(
  () => !!(window.stencil && window.stencil.imageSize) && location.search === '' && location.hash === '',
  null, { timeout: 15_000 });

test('a blank launch sets colour, size, name, theme, accent and a setting, then strips the query', async ({ page }) => {
  await gotoApp(page, { search: '?blank=%23336699&width=400&height=300&name=Sketch&theme=dark&main-color=yellow&thickness=7' });
  await launched(page);
  const state = await page.evaluate(() => ({
    size: window.stencil.imageSize,
    name: window.stencil.current && window.stencil.current.name,
    blankColor: window.stencil.current && window.stencil.current.blankColor,
    theme: document.documentElement.getAttribute('data-theme'),
    accent: window.stencil.mainTheme,
    thickness: window.stencil.thickness,
    incognito: window.stencil.incognito,
  }));
  expect(state.size.width).toBeGreaterThan(0);
  expect(state.name).toBe('Sketch');
  expect(state.blankColor).toBe('#336699');
  expect(state.theme).toBe('dark');
  expect(state.accent).toBe('yellow');
  expect(state.thickness).toBe(7);
  expect(state.incognito).toBe(false);
});

test('a source launch fetches the served picture and keeps its URL as the project source', async ({ page }) => {
  const src = `${SITE_URL}common/samples/sample.png`;
  await gotoApp(page, { search: `?source=${encodeURIComponent(src)}&name=Sample` });
  await launched(page);
  const state = await page.evaluate(() => ({
    size: window.stencil.imageSize,
    source: window.stencil.current && window.stencil.current.source,
    name: window.stencil.current && window.stencil.current.name,
    saved: window.stencil.getProjects({ archived: true }).length,
  }));
  expect(state.size.width).toBeGreaterThan(0);
  expect(state.source).toBe(src);
  expect(state.name).toBe('Sample');
  expect(state.saved).toBe(1);
});

test('an incognito launch saves no project, and a credential rides the fragment only', async ({ page }) => {
  await gotoApp(page, {
    search: '?blank=red&incognito&llm-api-key=k',
    hash: '#llm-provider=anthropic&llm-api-key=sk-e2e-test-1234',
  });
  await launched(page);
  const state = await page.evaluate(() => ({
    incognito: window.stencil.incognito,
    saved: window.stencil.getProjects({ archived: true }).length,
    provider: window.stencil.llm.provider,
    key: window.stencil.llm.apiKey,
    stored: localStorage.getItem('drawingApp_llmSettings') || '',
  }));
  expect(state.incognito).toBe(true);
  expect(state.saved).toBe(0);
  expect(state.provider).toBe('anthropic');
  expect(state.key).toBe('[redacted]');
  expect(state.stored.includes('sk-e2e')).toBe(false);
});

test('a bare ?open= is no launch: the project deep link strips it and nothing opens', async ({ page }) => {
  await gotoApp(page, { search: '?open=nowhere' });
  const state = await page.evaluate(() => ({ search: location.search, picture: !!window.stencil.imageSize }));
  expect(state.search).toBe('');
  expect(state.picture).toBe(false);
});
