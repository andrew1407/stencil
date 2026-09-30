// What a copy carries, by scope: the original image always, with its blank colour and
// provenance; the layout from `layout` up; the project's own colour, words, expiry and chat
// for `project`. A copy is always a detached, fresh row — never a server link.
import { addPeriod, DEFAULT_PERIOD } from '../store/projectsStore.js';
import { layoutLineLengthCm } from '../../settings/units.js';

const PROVENANCE = Object.freeze(['imageBaseName', 'imageExt', 'imageSource', 'imageResource']);

// The untouched original: no lines, crop, turn, filter or formulas — what a fresh load gives.
const freshLayout = (layout) => {
  const out = { lines: [] };
  for (const k of PROVENANCE) if (layout[k] != null) out[k] = layout[k];
  return out;
};
const wholeLayout = (layout) => JSON.parse(JSON.stringify({ ...layout, lines: layout.lines || [] }));

const LAYOUT_OF = Object.freeze({ image: freshLayout, layout: wholeLayout, project: wholeLayout });

const OWN_META = Object.freeze({
  image: () => ({}),
  layout: () => ({}),
  project: (m) => ({
    color: m.color || '', description: m.description || '',
    keywords: Array.isArray(m.keywords) ? [...m.keywords] : [],
    expiresAt: m.expiresAt ?? 0, refreshPeriod: m.refreshPeriod || DEFAULT_PERIOD, autoRefresh: m.autoRefresh ?? true,
  }),
});

export const copyScopes = (what) => ({ layout: what !== 'image', meta: what === 'project', chat: what === 'project' });

// The new row + payload for `src` (a CopySource) under `id` and `name`; the thumbnail is the caller's.
export const copyPayload = (src, what, { id, name, now = Date.now() }) => {
  const layout = LAYOUT_OF[what](src.payload.layout || {});
  const sized = what !== 'image';
  const m = src.meta || {};
  const meta = {
    id, name, color: '', description: '', keywords: [], thumbnail: null, createdAt: now,
    expiresAt: addPeriod(now, DEFAULT_PERIOD), refreshPeriod: DEFAULT_PERIOD, autoRefresh: true,
    hasImage: !!src.payload.image,
    imageW: sized ? (m.imageW || layout.imageWidth || 0) : 0,
    imageH: sized ? (m.imageH || layout.imageHeight || 0) : 0,
    lineLengthCm: layoutLineLengthCm(layout),
    blank: !!m.blank, blankColor: m.blankColor || '', fromFile: false,
    source: m.source || layout.imageSource || null, resource: m.resource || layout.imageResource || null,
    address: null, remoteId: null, remoteVersion: 0,
    ...OWN_META[what](m),
  };
  return { meta, payload: { image: src.payload.image, layout } };
};
