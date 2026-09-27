// The options page's Provider select is built from providers.json at load: the same values, order
// and labels the page carried as markup, and a provider added to the table appears on its own.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import PROVIDERS_ASSET from '../../src/config/providers.json' with { type: 'json' };
import { providerOptions } from '../../src/options/providerOptions.js';

test('the choices are the off state, then every provider in providers.json order', () => {
  assert.deepEqual(providerOptions().map(([id]) => id), ['none', ...Object.keys(PROVIDERS_ASSET.providers)]);
});

test('each choice keeps the label the page has always shown', () => {
  assert.deepEqual(providerOptions(), [
    ['none', 'None (turned off)'],
    ['ollama', 'Ollama'],
    ['openai-compat', 'OpenAI API (LM Studio, vLLM, …)'],
    ['anthropic', 'Anthropic API (Claude)'],
    ['stencil-server', 'Stencil collaboration server'],
  ]);
});

// The default URLs the page names come from providers.json, and read as the page always read.
test('the Base URL help and placeholder name each provider\'s default from providers.json', async () => {
  const { readFileSync } = await import('node:fs');
  const { fillProviderUrls } = await import('../../src/options/providerOptions.js');
  const html = readFileSync(new URL('../../src/options/options.html', import.meta.url), 'utf8');
  const nodes = [...html.matchAll(/<(code|input)\b[^>]*data-provider-url="([^"]+)"[^>]*>/g)]
    .map(([, tag, id]) => ({ tagName: tag.toUpperCase(), dataset: { providerUrl: id }, textContent: '', placeholder: '' }));
  fillProviderUrls({ querySelectorAll: () => nodes });
  let i = 0;
  const filled = html.replace(/<(code|input)\b([^>]*)data-provider-url="[^"]+"([^>]*)>(<\/code>)?/g, (m, tag, a, b, end) => {
    const n = nodes[i++];
    return tag === 'input' ? `<input${a}placeholder="${n.placeholder}"${b.trimEnd()}>` : `<code>${n.textContent}${end}`;
  });
  assert.ok(filled.includes('<input id="llm-baseurl" type="url" placeholder="http://localhost:11434">'));
  assert.ok(filled.includes('Pre-filled with the provider\'s default (<code>http://localhost:11434</code> for Ollama, '
    + '<code>http://localhost:1234/v1</code> for OpenAI-compatible); edit freely.'));
});
