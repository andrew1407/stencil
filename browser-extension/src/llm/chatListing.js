// ── The context listing the model reads (contract §8) ───────────────────────
// The scanned-image set and the open-tabs set as prompt text, each bounded and
// truncated, plus the routing that maps a dropped URL back to a listing index.
import { sameSource } from '../lib/drop/dropEntry.js';
import { sourceOf } from '../lib/image/imageModel.js';

// Context-listing bounds (contract §8): ≤ 100 entries, names/alt text truncated.
export const LISTING_LIMIT = 100;
export const LISTING_NAME_CHARS = 48;
export const LISTING_ALT_CHARS = 64;
// Open-tabs listing bounds (contract §8): ≤ 20 entries, titles/URLs truncated.
export const TABS_LIMIT = 20;
export const TAB_TITLE_CHARS = 64;
const TAB_URL_CHARS = 80;

const truncate = (s, max) => {
  const v = String(s || '');
  return v.length > max ? v.slice(0, max - 1) + '…' : v;
};

// §8 listing kind for a scanned record (imageScan.js shape: kind 'img'|'bg'|'video'
// plus the meta/poster flags): img | background | poster | video | icon.
export const listingKind = (item) => {
  if (!item) return 'img';
  if (item.meta) return 'icon';
  if (item.poster) return 'poster';
  if (item.kind === 'video') return 'video';
  if (item.kind === 'bg') return 'background';
  return 'img';
};

// The listed URL basename: a video keys on its media URL (the still is an opaque
// data URL); data: URLs have no meaningful basename.
const basenameOf = (item) => {
  const url = item.kind === 'video' ? (item.videoUrl || '') : (item.src || '');
  if (!url || url.startsWith('data:')) return item.kind === 'video' ? '(in-page video)' : '(inline data)';
  try {
    const u = new URL(url);
    return decodeURIComponent(u.pathname.split('/').filter(Boolean).pop() || u.hostname);
  } catch {
    return url;
  }
};

// Compact numbered listing for the system-prompt suffix (contract §8), capped at
// LISTING_LIMIT. `formatOfItem` is injected to keep this module pure.
export const buildListing = (items, { formatOfItem = () => '' } = {}) => {
  const all = Array.isArray(items) ? items : [];
  const lines = all.slice(0, LISTING_LIMIT).map((it, i) => {
    const parts = [`${i}: ${listingKind(it)}`];
    if (it.w > 0 && it.h > 0) parts.push(`${it.w}x${it.h}`);
    const fmt = formatOfItem(it);
    if (fmt) parts.push(fmt);
    parts.push(`"${truncate(basenameOf(it), LISTING_NAME_CHARS)}"`);
    if (it.alt) parts.push(`alt "${truncate(it.alt, LISTING_ALT_CHARS)}"`);
    return parts.join(' ');
  });
  if (all.length > LISTING_LIMIT) lines.push(`(+${all.length - LISTING_LIMIT} more not listed)`);
  return lines.join('\n');
};

// origin + path only: query/fragment hold session ids and search terms, and a length cut
// would keep the FRONT of a query string.
const tabUrlForModel = (raw) => {
  const u = (raw || '').trim();
  if (!u) return '';
  try {
    const parsed = new URL(u);
    return parsed.origin + parsed.pathname;
  } catch {
    return u.split(/[?#]/)[0];
  }
};

// Listing of the user's OTHER open tabs (contract §8), built only on the `shareTabs` opt-in.
// Tab titles/URLs are page-controlled DATA; they ride the suffix as text only.
export const buildTabsListing = (tabs) => {
  const all = Array.isArray(tabs) ? tabs : [];
  const lines = all.slice(0, TABS_LIMIT).map((t, i) => {
    const title = truncate((t && t.title) || '(untitled)', TAB_TITLE_CHARS);
    const url = truncate(tabUrlForModel(t && t.url), TAB_URL_CHARS);
    return `${i}: "${title}"${url ? ` — ${url}` : ''}`;
  });
  if (all.length > TABS_LIMIT) lines.push(`(+${all.length - TABS_LIMIT} more not listed)`);
  return lines.join('\n');
};

// ── Dropped-attachment routing ──────────────────────────────────────────────

// Index of the scan-listing entry a dropped URL refers to, or -1. `sameSource` is
// fragment-insensitive and '' never matches.
export const matchListingIndex = (items, url) =>
  (Array.isArray(items) ? items : []).findIndex((it) => it && sameSource(sourceOf(it), url));

// Text note describing the user's attachments, appended to the turn text so the
// model can connect the attached images back to listing indices (data, not markup).
export const attachmentNote = (attachments) => {
  const parts = (Array.isArray(attachments) ? attachments : []).map((a) => (
    Number.isInteger(a.index) && a.index >= 0
      ? `image ${a.index} from the listing${a.name ? ` (${a.name})` : ''}`
      : (a.name || 'an image')));
  return parts.length ? `[The user attached: ${parts.join('; ')}]` : '';
};
