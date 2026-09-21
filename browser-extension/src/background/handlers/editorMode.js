// What only chrome.* can answer for the panel and `stencil.extension`: which editor tabs
// are open and what each holds, which other tabs are scannable, one tab's images, and an
// import INTO an open editor. Every handler here answers (request/response).
import { getSettings, editorOriginPattern, fetchAsDataUrl, filenameFromUrl, focusTab } from '../../lib/stencil.js';
import { isEditorTab, editorRow, sourceTabChoices, importModeFor } from '../../lib/menu/editorTabs.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../../lib/image/imageScan.js';
import { sourceOf, editableSrc } from '../../lib/image/imageModel.js';
import { recordOpened } from '../../lib/prefs/ledger.js';
import { MSG } from '../../lib/messages.js';
import { answers, privileged, getTab, askEditorTab, probeEditorTabs, targetTabId, editorTabFor } from '../editorRelay.js';

// 'ask' is resolved HERE (query the target's state) and never reaches the editor page.
const IMPORT_MODES = ['new', 'replace', 'replace-keep', 'ask'];

export const editorModeHandlers = {
  // A silent bridge is listed ANYWAY with ready:false, or it'd hide a tab the user is looking at.
  [MSG.EDITOR_LIST]: answers(privileged(async (msg, sender) => {
    const pattern = await editorOriginPattern();
    if (!pattern) return { ok: false, error: 'no editor URL configured' };
    const tabs = await chrome.tabs.query({ url: [pattern] });
    const currentTabId = targetTabId(msg, sender);
    // A poll refresh asks for rows without thumbnails and keeps the previews it already has.
    const states = await probeEditorTabs(tabs, { thumbnail: msg.thumbnails !== false });
    const rows = tabs.map((tab, i) => editorRow(tab, states[i] && states[i].ok ? states[i].state : null, { currentTabId }));
    return {
      ok: true,
      // A non-answering, non-loading tab is an ordinary page on the editor origin — dropped.
      editors: rows.filter((row, i) => row.ready || (tabs[i] && tabs[i].status === 'loading')),
    };
  })),
  // Editor tabs and un-scannable schemes filtered out, so the panel and console agree.
  [MSG.SOURCE_TABS]: answers(privileged(async (msg) => {
    const { editorUrl } = await getSettings();
    const tabs = await chrome.tabs.query(msg.currentWindowOnly ? { currentWindow: true } : {});
    // Only same-origin tabs are asked which are REALLY editors — a couple of cheap round-trips.
    const pattern = await editorOriginPattern();
    const sameOrigin = pattern ? tabs.filter((t) => t.id != null && isEditorTab(t.url || '', editorUrl)) : [];
    const answered = await probeEditorTabs(sameOrigin);
    const editorTabIds = sameOrigin.filter((_, i) => answered[i] && answered[i].ok).map((t) => t.id);
    return { ok: true, tabs: sourceTabChoices(tabs, { editorUrl, editorTabIds }) };
  })),
  // The popup's own scanner, so naming/filtering stay in the panel, as in popup.js scan().
  [MSG.SCAN_TAB]: answers(privileged(async (msg) => {
    if (typeof msg.tabId !== 'number') return { ok: false, error: 'no tab' };
    const tab = await getTab(msg.tabId);
    if (!tab) return { ok: false, error: 'no such tab' };
    const url = tab.url || '';
    if (!url || BLOCKED_SCHEMES.some((s) => url.startsWith(s))) return { ok: false, error: 'this page can’t be scanned' };
    const limit = Number(msg.limit) > 0 ? Math.min(Math.trunc(Number(msg.limit)), MAX_IMAGES) : MAX_IMAGES;
    try {
      const results = await chrome.scripting.executeScript({
        target: { tabId: msg.tabId, allFrames: true }, func: scanPageForImages, args: [limit]
      });
      // The scanned tab's URL rides back as `resource`: the caller isn't on that tab to look it up.
      return { ok: true, tabId: msg.tabId, url, images: mergeScanFrames(results, limit) };
    } catch (err) {
      return { ok: false, error: `could not read this page (${err?.message || err})` };
    }
  })),
  // Import an image INTO an already-open editor tab, no navigation. Bytes are resolved here
  // (host permissions bypass page CORS) and handed to the tab's bridge.
  [MSG.EDITOR_IMPORT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    // A missing mode means 'ask' — the one value that can never overwrite work unasked.
    let mode = msg.mode || 'ask';
    if (!IMPORT_MODES.includes(mode)) return { ok: false, error: 'unknown import mode' };
    if (mode === 'ask') {
      // Only hasImage matters here, so skip the canvas capture: this runs on EVERY import.
      const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: false });
      const state = reply.ok ? reply.state : null;
      // Occupied editor → hand the state back so the panel can raise its chooser.
      if (importModeFor(state) === 'ask')
        return { ok: false, error: 'editor already holds an image', needsChoice: true, state };
      mode = 'new';
    }
    const image = (msg.image && typeof msg.image === 'object') ? msg.image : {};
    const src = editableSrc(image) || sourceOf(image);
    // Guard context is the content-script sender's own tab URL, never page-supplied data.
    const dataUrl = await fetchAsDataUrl(src || '', { pageUrl: sender.tab?.url || msg.resource || '' });
    const { page, editorUrl } = await getSettings();
    const payload = buildHandoff(
      { ...image, name: image.name || filenameFromUrl(src) },
      { dataUrl, page: msg.page || page, resource: msg.resource || '', incognito: !!msg.incognito }
    );
    if (msg.crop) payload.crop = msg.crop;
    const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_IMPORT, payload, mode });
    if (!reply.ok) return reply;
    // A ledger write must not fail an import that LANDED, so it's best-effort.
    if (!payload.incognito) {
      await recordOpened({ source: payload.source, resource: payload.resource, name: payload.name, editorUrl })
        .catch(() => { /* storage unavailable — badges just won't show */ });
    }
    return { ok: true, tabId: tab.id, mode, projectId: reply.projectId || '', projectName: reply.projectName || '' };
  }),
  // Switch an editor tab to one of ITS OWN projects by id; the reply is passed straight through.
  [MSG.EDITOR_SWITCH_PROJECT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_SWITCH_PROJECT, projectId: String(msg.projectId || '') });
  }),
  // Only the tab's own bridge answering proves it IS an editor (an origin match alone doesn't).
  [MSG.EDITOR_STATE]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: msg.thumbnail !== false, thumbMax: msg.thumbMax });
  }),
  // Not restricted to editor tabs: the source-tab picker offers "go look at that page" too.
  [MSG.EDITOR_FOCUS_TAB]: answers(privileged(async (msg) => {
    if (typeof msg.tabId !== 'number') return { ok: false, error: 'no tab' };
    const tab = await getTab(msg.tabId);
    if (!tab) return { ok: false, error: 'no such tab' };
    try {
      await focusTab(tab);
    } catch {
      return { ok: false, error: 'no such tab' };   // closed between the get and the update
    }
    return { ok: true, tabId: tab.id, windowId: tab.windowId != null ? tab.windowId : null };
  })),
};
