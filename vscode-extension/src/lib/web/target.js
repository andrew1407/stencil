// The ONE way the browser instance is named: EXPLICIT USER CONFIGURATION, like the CLI path
// — the `stencil.webUrl` setting, else the default — never a URL out of the open document.
import { CONFIG_SECTION, DEFAULT_WEB_URL, SETTINGS } from '../ids.js';

const BAD_WEB_URL = 'stencil.webUrl must be an http(s) URL';

// '' for anything not http(s); a fragment is dropped, since a hand-off appends its own.
const normalizeWebUrl = (value) => {
  const text = String(value ?? '').trim();
  if (!text) return DEFAULT_WEB_URL;
  let url;
  try {
    url = new URL(text);
  } catch {
    return '';
  }
  if (url.protocol !== 'http:' && url.protocol !== 'https:') return '';
  url.hash = '';
  return url.toString();
};

const webUrlFor = (vscode) => normalizeWebUrl(
  vscode.workspace.getConfiguration(CONFIG_SECTION).get(SETTINGS.webUrl, ''),
);

export { BAD_WEB_URL, normalizeWebUrl, webUrlFor };
