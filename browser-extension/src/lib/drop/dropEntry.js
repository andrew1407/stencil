// A DROPPED payload normalised into the scan-row shape (`{kind, src, videoUrl?, name, w, h,
// opened, pinned, …}`) so the drop targets reuse the row machinery.
import { filenameFromUrl } from '../stencil.js';
import { guessKindFromUrl } from './dragUrl.js';
import { isVideoFile, isDropCandidate } from '../chat/chatDrop.js';
import { sourceOf } from '../image/imageModel.js';

// Our own rows carry this drag TYPE, which stays readable mid-drag (the data never is).
export const INTERNAL_DRAG_TYPE = 'application/x-stencil-drag';

// From the DataTransfer's TYPES alone (all a `dragover` exposes): 'internal' | 'files' |
// 'url' | '' for anything we can't act on.
export const dragPayloadKind = (types) => {
  const t = Array.isArray(types) ? types : (types ? [...types] : []);
  if (!t.length) return '';
  if (t.includes(INTERNAL_DRAG_TYPE)) return 'internal';
  if (!isDropCandidate(t)) return '';
  return t.includes('Files') ? 'files' : 'url';
};

// `setArmed(on)` is called only on a CHANGE (dragover fires continuously).
export const createDragArmer = ({ setArmed }) => {
  let armed = false;
  const set = (on) => {
    if (on === armed) return;
    armed = on;
    setArmed(on);
  };
  return {
    isArmed: () => armed,
    update(types) { set(!!dragPayloadKind(types)); },
    end() { set(false); },
  };
};

// Equal ignoring only the #fragment, so a drop reuses the scanned row.
export const sameSource = (a, b) => {
  if (!a || !b) return false;
  if (a === b) return true;
  const strip = (u) => { try { const x = new URL(u); x.hash = ''; return x.href; } catch { return u; } };
  return strip(a) === strip(b);
};

export const isMediaFile = (file) =>
  !!file && ((typeof file.type === 'string' && file.type.startsWith('image/')) || isVideoFile(file));

// A video keys on its media URL with no still (`src: ''`), exactly as the scanner
// represents a frameless video. `{kind}` overrides the extension guess.
export const entryFromUrl = (src, { name = '', kind = '' } = {}) => {
  const url = String(src || '').trim();
  if (!url) return null;
  const k = kind || guessKindFromUrl(url);
  const label = name || filenameFromUrl(url, k === 'video' ? 'video' : 'image');
  const base = { name: label, w: 0, h: 0, alt: '', opened: [], pinned: false };
  return k === 'video'
    // A frameless video has nothing to measure.
    ? { ...base, kind: 'video', src: '', videoUrl: url, measured: true }
    : { ...base, kind: 'img', src: url, measured: false };
};

// A classified drop (chatDrop.js) into a row entry: a URL prefers its SCANNED entry.
export const entryFromDrop = (payload, { items = [], objectUrl = null } = {}) => {
  if (!payload) return null;
  if (payload.kind === 'files') {
    const file = (payload.files || []).find(isMediaFile);
    if (!file || !objectUrl) return null;
    const url = objectUrl(file);
    if (!url) return null;
    return entryFromUrl(url, { name: file.name || '', kind: isVideoFile(file) ? 'video' : 'img' });
  }
  const src = String(payload.url || '').trim();
  if (!src) return null;
  const existing = (items || []).find((im) => sameSource(sourceOf(im), src));
  return existing || entryFromUrl(src);
};

// The menu under the logo MID-DRAG: four flat drop targets. `needsPixels` marks the
// actions that need something drawable.
export const DRAG_MENU_ACTIONS = Object.freeze([
  { id: 'editor', label: 'Open in editor', icon: 'monitor', needsPixels: true },
  { id: 'newtab', label: 'Open in new tab', icon: 'external', needsPixels: false },
  { id: 'incognito', label: 'Open incognito', icon: 'incognito', needsPixels: true },
  { id: 'crop', label: 'Crop', icon: 'crop', needsPixels: true },
]);

// A null `entry` (a `dragover` exposes only types) opens the menu optimistically; the
// drop re-checks against the real entry.
export const dragMenuActions = (entry) => {
  if (entry === null || entry === undefined) return DRAG_MENU_ACTIONS.slice();
  const media = sourceOf(entry);
  const pixels = !!(entry.src || entry.posterUrl);
  return DRAG_MENU_ACTIONS.filter((a) => (a.needsPixels ? pixels : !!media));
};

// Refuses a stale optimistic item at drop time.
export const dragActionAllowed = (entry, id) => dragMenuActions(entry).some((a) => a.id === id);
