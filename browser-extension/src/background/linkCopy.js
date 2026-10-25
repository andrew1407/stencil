// "Copy Stencil link": a link that opens the Stencil app (Options' share URL) on the clicked image,
// background or video, put on the clipboard, with a short note in the page saying what happened.
import { getSettings } from '../lib/stencil.js';
import { MENU, INCOGNITO_LINK_ITEMS } from '../lib/menu/contextMenu.js';
import { shareLink } from '../lib/menu/shareLink.js';
import { lastTargetByTab } from './tabState.js';
import { copyText } from './clipboard.js';

const BG = new Set([MENU.BG_LINK, MENU.BG_LINK_INCOGNITO]);
// A background's address is the probe's; an <img> or <video> carries its own as srcUrl.
const sourceOf = (info, rec) => (BG.has(info.menuItemId) ? (rec && rec.url) || info.srcUrl : info.srcUrl || (rec && rec.url)) || '';

export const NOTES = Object.freeze({
  copied: 'Stencil link copied',
  copiedIncognito: 'Stencil link (incognito) copied',
  unshareable: 'This one has no web address another browser can open',
  failed: 'Could not copy the link',
});

// Runs in the page (isolated world): a toast in a closed shadow root, gone after 1.8 s.
const toast = (text) => {
  const host = document.createElement('div');
  host.style.cssText = 'position:fixed;right:16px;bottom:16px;z-index:2147483647;pointer-events:none';
  const root = host.attachShadow({ mode: 'closed' });
  const note = document.createElement('div');
  note.textContent = text;
  note.style.cssText = 'font:13px/1.3 system-ui,sans-serif;color:#fff;background:#1a1a1a;padding:8px 12px;border-radius:8px;box-shadow:0 4px 16px rgba(0,0,0,.3)';
  root.appendChild(note);
  document.documentElement.appendChild(host);
  setTimeout(() => host.remove(), 1800);
};

const tell = async (tabId, frameId, text) => {
  if (tabId == null) return;
  const target = { tabId };
  if (frameId != null) target.frameIds = [frameId];
  try { await chrome.scripting.executeScript({ target, func: toast, args: [text] }); } catch { /* a page scripts cannot reach */ }
};

export const copyLinkFromMenu = async (info, tab, tabId) => {
  const rec = tabId != null ? lastTargetByTab.get(tabId) : null;
  const incognito = INCOGNITO_LINK_ITEMS.includes(info.menuItemId);
  const { shareUrl } = await getSettings();
  const link = shareLink(shareUrl, sourceOf(info, rec), { incognito });
  if (!link) { await tell(tabId, info.frameId, NOTES.unshareable); return null; }
  const ok = await copyText(link);
  await tell(tabId, info.frameId, ok ? (incognito ? NOTES.copiedIncognito : NOTES.copied) : NOTES.failed);
  return ok ? link : null;
};
