// The assistant driving the panel's own controls (llm-contract.md §8).
import { icon } from '../lib/icons.js';
import { pinnable } from '../lib/image/imageModel.js';
import { ASSISTANT_SECTION } from '../lib/drop/dragSections.js';
import { loadLlmSettings, assistantEnabled, LLM_SETTINGS_KEY } from '../llm/llmSettings.js';
import { createAssistant, applyAssistantVisibility } from './assistant.js';
import { themePref } from './panelDom.js';
import { state } from './list/model.js';
import { filterUi, applyFilters } from './list/filters.js';
import { scan } from './list/scan.js';
import { setPinnedState } from './pin/pinActions.js';
import { editorMode } from './editor/editorHandle.js';
import { sections } from './list/sections.js';

// §8 theme / filter ops go through the same DOM controls a click would use, so the
// controls, persisted state and list can never disagree with the model.
const KIND_CONTROL = {
  images: 'f-img', css: 'f-bg', video: 'f-video', posters: 'f-poster', meta: 'f-meta',
};
const assistantSetTheme = (mode) => {
  if (!themePref) throw new Error('the theme cannot be changed here');
  themePref.set(mode, document.getElementById('theme-toggle'));
};
const assistantSetFilters = (patch) => {
  const applied = [];
  const setValue = (id, v) => { const el = document.getElementById(id); if (el) el.value = v; };
  if (patch.search != null) { setValue('f-search', patch.search); applied.push(patch.search ? `search "${patch.search}"` : 'search cleared'); }
  if (patch.regex != null) {
    const el = document.getElementById('f-regex');
    if (el) { el.checked = patch.regex; applied.push(`regex ${patch.regex ? 'on' : 'off'}`); }
  }
  if (patch.kinds) {
    // The model states the whole set it wants shown, not one toggle.
    const want = new Set(patch.kinds);
    for (const [kind, id] of Object.entries(KIND_CONTROL)) {
      const el = document.getElementById(id);
      if (el) el.checked = want.has(kind);
    }
    applied.push(`showing ${patch.kinds.join(', ') || 'nothing'}`);
  }
  if (patch.formats) {
    const all = patch.formats.includes('*');
    const want = new Set(patch.formats);
    for (const cb of filterUi.checkboxes()) cb.checked = all || want.has(String(cb.value).toUpperCase());
    filterUi.updateToggleLabel();
    applied.push(all ? 'all formats' : `formats ${patch.formats.join(', ') || 'cleared'}`);
  }
  // A bound of 0 clears it: an empty input is "no bound".
  for (const [key, id, label] of [['minWidth', 'f-minw', 'min width'], ['maxWidth', 'f-maxw', 'max width'],
    ['minHeight', 'f-minh', 'min height'], ['maxHeight', 'f-maxh', 'max height']]) {
    if (patch[key] == null) continue;
    setValue(id, patch[key] ? String(patch[key]) : '');
    applied.push(patch[key] ? `${label} ${patch[key]}px` : `${label} cleared`);
  }
  // Firing the checkbox's own change handler runs the exact path a click takes.
  for (const [key, id, label] of [['markOpened', 'f-mark-opened', 'mark opened'],
    ['openedFirst', 'f-opened-first', 'opened first'], ['showPinned', 'f-show-pinned', 'show pinned']]) {
    if (patch[key] == null) continue;
    const el = document.getElementById(id);
    if (el) { el.checked = patch[key]; el.dispatchEvent(new Event('change')); }
    applied.push(`${label} ${patch[key] ? 'on' : 'off'}`);
  }
  applyFilters();
  return { applied };
};

// The accent store holds preset KEYS only, so a raw "#rrggbb" resolves to the closest preset.
const assistantSetAccent = ({ color, preset }) => {
  const accentPref = window.StencilAccent;
  if (!accentPref) throw new Error('the accent cannot be changed here');
  let hit = null, exact = true;
  if (preset != null) {
    const want = preset.toLowerCase();
    hit = accentPref.list.find((a) => a.key === want || a.label.toLowerCase() === want);
    if (!hit) throw new Error(`unknown accent preset "${preset}"`);
  } else {
    const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
    const want = rgb(color);
    let bestD = Infinity;
    for (const a of accentPref.list) {
      const c = rgb(a.hex);
      const d = (c[0] - want[0]) ** 2 + (c[1] - want[1]) ** 2 + (c[2] - want[2]) ** 2;
      if (d < bestD) { bestD = d; hit = a; }
    }
    exact = bestD === 0;
  }
  accentPref.set(hit.key, document.getElementById('theme-toggle'));
  return { label: hit.label, exact };
};

const assistant = createAssistant({
  getItems: () => state.all,
  getTabId: () => state.activeTabId,
  getPageUrl: () => state.activeUrl,
  openHere: (entry, opts) => (state.mode === 'editor' ? editorMode.importHere(entry, opts) : false),
  // Same gate as the row's pin button; a scanTab'd entry pins on its own site via `resource`.
  pinImage: (entry) => {
    if (!pinnable(entry)) throw new Error('this image has no stable source URL to pin');
    return setPinnedState(entry, true);
  },
  unpinImage: (entry) => {
    if (!pinnable(entry)) throw new Error('this image has no stable source URL to pin');
    return setPinnedState(entry, false);
  },
  rescan: () => scan(),
  setTheme: assistantSetTheme,
  setFilters: assistantSetFilters,
  setAccent: assistantSetAccent,
});
sections.setHook(ASSISTANT_SECTION, (collapsed) => assistant.handleToggle(collapsed));
const chatBtn = document.getElementById('open-chat');
chatBtn.innerHTML = icon('sparkle');
chatBtn.addEventListener('click', () => assistant.reveal());

// Provider 'none' (contract §5) hides the section; it stays in the DOM so Options
// re-enables it live.
const applyAssistantGate = async () => {
  applyAssistantVisibility(assistantEnabled(await loadLlmSettings()), {
    section: document.getElementById(ASSISTANT_SECTION),
    button: chatBtn,
  });
};
applyAssistantGate();
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === 'local' && changes[LLM_SETTINGS_KEY]) applyAssistantGate();
});
