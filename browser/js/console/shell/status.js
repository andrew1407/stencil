// The header line under the logo, as the Zig console prints it (cli/src/console/ui.zig
// status()): `image: <name> (<w>x<h> px · <page>)  [in-memory, temporary]  [n/m]`.
import { getPageDimensions } from '../../core/parse/pageMetrics.js';

export const NO_IMAGE_STATUS = 'image: (none) — upload, paste, or create one first';

const round1 = (v) => Math.round(v * 10) / 10;

// `A4 29.7×21cm`, the format oriented as the page is.
export const pageLabel = (app) => {
  const dims = getPageDimensions(app);
  const name = String(app.pageSize || 'A4');
  return `${name === 'custom' ? 'custom' : name.toUpperCase()} ${round1(dims.width)}×${round1(dims.height)}cm`;
};

export const imageName = (app) => {
  const meta = app.activeProjectId != null ? app.storage?.store?.getMeta?.(app.activeProjectId) : null;
  const base = meta?.name || app.imageBaseName || 'image';
  const ext = meta?.imageExt || app.imageExt || '';
  return ext && !base.endsWith(`.${ext}`) ? `${base}.${ext}` : base;
};

// `color` is the project's name colour ('' = the default grey); `temporary` marks an
// incognito or unsaved session, as the CLI marks an in-memory one.
export const statusLine = (app) => {
  const img = app.image;
  if (!img) return { text: NO_IMAGE_STATUS, name: '', color: '', temporary: false };
  const name = imageName(app);
  const meta = app.activeProjectId != null ? app.storage?.store?.getMeta?.(app.activeProjectId) : null;
  const temporary = !!app.storage?.incognito || app.activeProjectId == null;
  // The pristine picture is the history's floor (step -1), so it counts as state 1.
  const h = app.history;
  const steps = (Array.isArray(h?.history) ? h.history.length : 0) + 1;
  const at = (typeof h?.historyStep === 'number' ? h.historyStep : -1) + 2;
  const pos = steps > 1 ? `  [${at}/${steps}]` : '';
  const tag = temporary ? '  [in-memory, temporary]' : '';
  return {
    text: `image: ${name} (${img.width}x${img.height} px · ${pageLabel(app)})${tag}${pos}`,
    name,
    color: meta?.color || '',
    temporary,
  };
};
