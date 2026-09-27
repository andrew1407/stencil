// The Provider select's choices on the options page: the off state, then providers.json in its
// own order, each under the name the client gives it; this page names the server by its role.
import { PROVIDERS, PROVIDER_BASE_URLS } from '../llm/settings.js';
import { PROVIDER_LABELS } from '../llm/client.js';

const LABELS = Object.freeze({ ...PROVIDER_LABELS, 'stencil-server': 'Stencil collaboration server' });

// [value, label] pairs, in the order the select lists them.
export const providerOptions = () => PROVIDERS.map((id) => [id, LABELS[id] || id]);

// Every `data-provider-url` node shows that provider's default base URL: a field as its placeholder.
export const fillProviderUrls = (root) => {
  for (const el of root.querySelectorAll('[data-provider-url]')) {
    const url = PROVIDER_BASE_URLS[el.dataset.providerUrl] || '';
    if (el.tagName === 'INPUT') el.placeholder = url;
    else el.textContent = url;
  }
};
