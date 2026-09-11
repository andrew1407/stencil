// ── Editor mode (request/response — every one of these answers) ─────────────
// What only chrome.* can answer for the panel and for `stencil.extension`: which editor
// tabs are open and what each holds, which other tabs are scannable, one tab's images,
// and an import INTO an open editor.
import { getSettings, editorOriginPattern, fetchAsDataUrl, filenameFromUrl, focusTab } from '../../lib/stencil.js';
import { isEditorTab, editorRow, sourceTabChoices, importModeFor } from '../../lib/editorTabs.js';
import { scanPageForImages, mergeScanFrames, MAX_IMAGES, BLOCKED_SCHEMES } from '../../lib/imageScan.js';
import { sourceOf, editableSrc } from '../../lib/imageModel.js';
import { recordOpened } from '../../lib/ledger.js';
import { MSG } from '../../lib/messages.js';
import { answers, privileged, getTab, askEditorTab, probeEditorTabs, targetTabId, editorTabFor } from '../editorRelay.js';

// The import modes a caller may ask for. 'ask' is resolved HERE (query the target's
// state, then import or bounce back with needsChoice) and never reaches the editor page.
const IMPORT_MODES = ['new', 'replace', 'replace-keep', 'ask'];

export const editorModeHandlers = {
  // Every open editor tab, each joined with the live state its bridge reports. A tab whose
  // bridge stays silent (old editor build, still loading, no extensionBridge.js) is listed
  // ANYWAY with ready:false — dropping it would hide an editor tab the user is looking at.
  [MSG.EDITOR_LIST]: answers(privileged(async (msg, sender) => {
    const pattern = await editorOriginPattern();
    if (!pattern) return { ok: false, error: 'no editor URL configured' };
    const tabs = await chrome.tabs.query({ url: [pattern] });
    const currentTabId = targetTabId(msg, sender);
    // A poll refresh asks for rows without thumbnails (thumbnails:false) and keeps the
    // previews it already has.
    const states = await probeEditorTabs(tabs, { thumbnail: msg.thumbnails !== false });
    const rows = tabs.map((tab, i) => editorRow(tab, states[i] && states[i].ok ? states[i].state : null, { currentTabId }));
    return {
      ok: true,
      // Origin matching can't tell the editor from an ordinary page served beside it —
      // those answer nothing and belong in SOURCE_TABS, so drop them. A tab still LOADING
      // has simply not answered yet, so it stays: the panel's poll fills it in a moment.
      editors: rows.filter((row, i) => row.ready || (tabs[i] && tabs[i].status === 'loading')),
    };
  })),
  // The other open pages an image can be pulled from (editor tabs and un-scannable
  // schemes filtered out by the shared helper, so the panel and the console agree).
  [MSG.SOURCE_TABS]: answers(privileged(async (msg) => {
    const { editorUrl } = await getSettings();
    const tabs = await chrome.tabs.query(msg.currentWindowOnly ? { currentWindow: true } : {});
    // Ask the editor-origin tabs which are REALLY editors, so an ordinary page on that
    // origin is still offered as a source (see sourceTabChoices). Only same-origin tabs
    // are asked, without thumbnails — a couple of cheap round-trips, not a full fan-out.
    const pattern = await editorOriginPattern();
    const sameOrigin = pattern ? tabs.filter((t) => t.id != null && isEditorTab(t.url || '', editorUrl)) : [];
    const answered = await probeEditorTabs(sameOrigin);
    const editorTabIds = sameOrigin.filter((_, i) => answered[i] && answered[i].ok).map((t) => t.id);
    return { ok: true, tabs: sourceTabChoices(tabs, { editorUrl, editorTabIds }) };
  })),
  // Scan a tab the caller is NOT standing on, with the popup's own scanner (same
  // all-frames executeScript + mergeScanFrames), so both surfaces see the same images.
  // Items come back raw: naming and filtering stay in the panel, as in popup.js scan().
  [MSG.SCAN_TAB]: answers(privileged(async (msg) => {
    if (typeof msg.tabId !== 'number') return { ok: false, error: 'no tab' };
    const tab = await getTab(msg.tabId);
    if (!tab) return { ok: false, error: 'no such tab' };
    const url = tab.url || '';
    if (!url || BLOCKED_SCHEMES.some((s) => url.startsWith(s))) return { ok: false, error: 'this page can’t be scanned' };
    // A caller-supplied limit is data: clamped to the scanner's own ceiling.
    const limit = Number(msg.limit) > 0 ? Math.min(Math.trunc(Number(msg.limit)), MAX_IMAGES) : MAX_IMAGES;
    try {
      const results = await chrome.scripting.executeScript({
        target: { tabId: msg.tabId, allFrames: true }, func: scanPageForImages, args: [limit]
      });
      // The scanned tab's URL rides back with the images: it's the `resource` (provenance)
      // a later EDITOR_IMPORT needs, and the caller isn't on that tab to look it up.
      return { ok: true, tabId: msg.tabId, url, images: mergeScanFrames(results, limit) };
    } catch (err) {
      return { ok: false, error: `could not read this page (${err?.message || err})` };
    }
  })),
  // Import an image INTO an already-open editor tab — no new tab, no navigation. Bytes are
  // resolved here (host permissions bypass page CORS) and handed to the tab's bridge as the
  // same buildHandoff payload the #stencil= launch path carries.
  [MSG.EDITOR_IMPORT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    // Mode is caller data: whitelist it before anything is fetched or replaced. A missing
    // mode means 'ask' — the one value that can never overwrite work on screen unasked.
    let mode = msg.mode || 'ask';
    if (!IMPORT_MODES.includes(mode)) return { ok: false, error: 'unknown import mode' };
    if (mode === 'ask') {
      // Only one boolean of the state matters here (hasImage), so skip the canvas capture:
      // a full state reply carries a 256px JPEG data URL across three hops, and this pre-check
      // runs on EVERY import — including the second one, after the chooser answers.
      const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: false });
      const state = reply.ok ? reply.state : null;
      // Occupied editor → don't import; hand the state back so the panel can raise the
      // chooser (new project / replace / replace keeping annotations) and the console API
      // can say why nothing happened. A silent bridge reads as blank → 'new'.
      if (importModeFor(state) === 'ask')
        return { ok: false, error: 'editor already holds an image', needsChoice: true, state };
      mode = 'new';
    }
    const image = (msg.image && typeof msg.image === 'object') ? msg.image : {};
    // Bytes from the openable source (a video row's captured still, an image's src),
    // provenance from sourceOf via buildHandoff — the split popup.js already makes.
    // fetchAsDataUrl enforces the http(s)/blob/data allowlist on this page-supplied URL.
    const src = editableSrc(image) || sourceOf(image);
    // Guard context: a content-script sender lends its own (browser-set) tab URL; an
    // extension surface (no sender.tab) is trusted to name the scanned page itself.
    const dataUrl = await fetchAsDataUrl(src || '', { pageUrl: sender.tab?.url || msg.resource || '' });   // '' / a bad scheme → 'unsupported URL scheme'
    const { page, editorUrl } = await getSettings();
    const payload = buildHandoff(
      { ...image, name: image.name || filenameFromUrl(src) },
      { dataUrl, page: msg.page || page, resource: msg.resource || '', incognito: !!msg.incognito }
    );
    // Crop rect (original-image pixels) is forwarded untouched; the editor applies it the
    // same way it does for a cropped launch payload.
    if (msg.crop) payload.crop = msg.crop;
    const reply = await askEditorTab(tab.id, { type: MSG.EDITOR_IMPORT, payload, mode });
    if (!reply.ok) return reply;
    // Write the same opened-ledger entry every other hand-off does — without it the panel
    // wouldn't badge the row and a second click would import a duplicate. Skipped for
    // incognito and best-effort: a ledger write must not fail an import that LANDED.
    if (!payload.incognito) {
      await recordOpened({ source: payload.source, resource: payload.resource, name: payload.name, editorUrl })
        .catch(() => { /* storage unavailable — badges just won't show */ });
    }
    return { ok: true, tabId: tab.id, mode, projectId: reply.projectId || '', projectName: reply.projectName || '' };
  }),
  // Switch an editor tab to one of ITS OWN projects (by id — the fire-and-forget
  // EDITOR_SWITCH above matches by source URL instead). The page refuses an unknown id
  // rather than clearing itself, and its reply is passed straight through.
  [MSG.EDITOR_SWITCH_PROJECT]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_SWITCH_PROJECT, projectId: String(msg.projectId || '') });
  }),
  // ONE tab's editor state. The panel asks this before flipping into editor mode: an origin
  // match alone also matches ordinary pages served beside the editor, and only the tab's own
  // bridge answering proves it IS one (popup.js resolveScanTab → editorMode.isLiveEditor).
  [MSG.EDITOR_STATE]: answers(async (msg, sender) => {
    const { tab, error } = await editorTabFor(msg, sender);
    if (error) return { ok: false, error };
    return askEditorTab(tab.id, { type: MSG.EDITOR_STATE, thumbnail: msg.thumbnail !== false, thumbMax: msg.thumbMax });
  }),
  // Bring a tab to the front (select it AND raise its window — focusTab, the same pair
  // resumeInOpenEditor uses). Not restricted to editor tabs: the source-tab picker offers
  // "go look at that page" too.
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
