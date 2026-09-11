// ── Dropped media → a scan-row entry ────────────────────────────────────────
// Everything the panel can act on is a "row": `{kind, src, videoUrl?, name, w, h, opened,
// pinned, …}` — what scanPageForImages produces and popup.js's ⋯ menu consumes. A DROPPED
// payload is normalised into that same shape so the drop targets reuse the row machinery.
// Pure and DOM-free: `URL.createObjectURL` is injected for the file case.
import { filenameFromUrl } from './stencil.js';
import { guessKindFromUrl } from './dragUrl.js';
import { isVideoFile, isDropCandidate } from './chatDrop.js';
import { sourceOf } from './imageModel.js';

// Our own list rows carry this drag type, which stays readable mid-drag (the
// DataTransfer's data never is) — see bindRowDrag in popup/popup.js.
export const INTERNAL_DRAG_TYPE = 'application/x-stencil-drag';

/**
 * What is being dragged, from the DataTransfer's TYPES alone (all a `dragover`
 * exposes): 'internal' for one of our rows, 'files' / 'url' for media coming from
 * outside, '' for anything we can't act on. Pure — the single classification the
 * logo target, the logo's pulse and the section spring all share.
 */
export const dragPayloadKind = (types) => {
  const t = Array.isArray(types) ? types : (types ? [...types] : []);
  if (!t.length) return '';
  if (t.includes(INTERNAL_DRAG_TYPE)) return 'internal';
  if (!isDropCandidate(t)) return '';
  return t.includes('Files') ? 'files' : 'url';
};

/**
 * Advertise that a drop target is live for as long as a COMPATIBLE drag is somewhere
 * over the surface — the logo pulses so you know it's a target before you get there.
 * `setArmed(on)` is called only on a CHANGE (dragover fires continuously).
 */
export const createDragArmer = ({ setArmed }) => {
  let armed = false;
  const set = (on) => {
    if (on === armed) return;
    armed = on;
    setArmed(on);
  };
  return {
    isArmed: () => armed,
    /** A drag moved over the surface: arm iff its payload is one we can act on. */
    update(types) { set(!!dragPayloadKind(types)); },
    /** The drag ended (drop / dragend / left the window). */
    end() { set(false); },
  };
};

// Two source URLs refer to the same image when they're equal ignoring only the
// #fragment (a dragged URL and the scanned currentSrc otherwise match byte-for-byte).
// Kept lenient on purpose so a drop reuses the scanned row instead of a stray duplicate.
export const sameSource = (a, b) => {
  if (!a || !b) return false;
  if (a === b) return true;
  const strip = (u) => { try { const x = new URL(u); x.hash = ''; return x.href; } catch { return u; } };
  return strip(a) === strip(b);
};

// A dropped File we can act on: an image or a video (MIME first, extension fallback).
export const isMediaFile = (file) =>
  !!file && ((typeof file.type === 'string' && file.type.startsWith('image/')) || isVideoFile(file));

/**
 * A minimal scan-shaped row for a media URL that isn't among the scanned images.
 * A video keys on its media URL and has no still (`src: ''`), which is exactly how the
 * scanner represents a frameless video — so the row machinery treats it identically.
 * `{name}` overrides the URL-derived name; `{kind}` overrides the extension guess (a
 * dropped File knows its own MIME type).
 */
export const entryFromUrl = (src, { name = '', kind = '' } = {}) => {
  const url = String(src || '').trim();
  if (!url) return null;
  const k = kind || guessKindFromUrl(url);
  const label = name || filenameFromUrl(url, k === 'video' ? 'video' : 'image');
  const base = { name: label, w: 0, h: 0, alt: '', opened: [], pinned: false };
  return k === 'video'
    // `measured: true` — a frameless video has nothing to measure.
    ? { ...base, kind: 'video', src: '', videoUrl: url, measured: true }
    : { ...base, kind: 'img', src: url, measured: false };
};

/**
 * Normalise a classified drop (lib/chatDrop.js `classifyDrop`) into a row entry, or null.
 *   files → the first image/video File, via an object URL (its MIME picks the kind)
 *   url   → a matching SCANNED entry when there is one (richer: real dims, format,
 *           opened/pinned state), else a fresh entry derived from the URL
 */
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

// ── The spring-loaded drag menu ─────────────────────────────────────────────
// The menu that opens under the logo MID-DRAG is deliberately tiny: four flat actions,
// each a drop target you can release onto. Download / Pin / Open in… / submenus live in
// the row's ⋯ menu, which a normal click reaches. `needsPixels` marks the actions that
// need something drawable.
export const DRAG_MENU_ACTIONS = [
  { id: 'editor', label: 'Open in editor', icon: 'monitor', needsPixels: true },
  { id: 'newtab', label: 'Open in new tab', icon: 'external', needsPixels: false },
  { id: 'incognito', label: 'Open incognito', icon: 'incognito', needsPixels: true },
  { id: 'crop', label: 'Crop', icon: 'crop', needsPixels: true },
];

/**
 * The drag-menu actions that apply to an entry, in menu order. Same guards as the row
 * menu: unknown DIMENSIONS cost nothing (the editor measures the bytes it fetches), but a
 * video with no captured frame keeps only the actions that don't need pixels — omitted
 * rather than offered-and-throwing. A null `entry` means the payload can't be read yet (a
 * `dragover` exposes only types), so the menu springs open optimistically and the drop
 * re-checks against the real entry.
 */
export const dragMenuActions = (entry) => {
  if (entry === null || entry === undefined) return DRAG_MENU_ACTIONS.slice();
  const media = sourceOf(entry);                              // video URL, or image src
  const pixels = !!(entry.src || entry.posterUrl);            // something drawable
  return DRAG_MENU_ACTIONS.filter((a) => (a.needsPixels ? pixels : !!media));
};

/** Does `id` apply to `entry`? Used at drop time to refuse a stale optimistic item. */
export const dragActionAllowed = (entry, id) => dragMenuActions(entry).some((a) => a.id === id);
