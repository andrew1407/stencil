// The edit half of the console's commands, each a facade call: what brings a picture in, the
// transforms, the colours and the saves. A handler returns the lines to print (or nothing) and
// throws the error to show; the interpreter prints both.
import { guardedFetch } from '../../net/fetchGuard.js';
import { readCapped } from '../../net/cappedBody.js';
import { normalizePageSize } from '../../core/settings/units.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { parseCropSpec, splitWords } from './grammar.js';

const FILTERS = ['none', 'bw', 'sepia', 'invert', 'contour'];
const MAX_UNDO = 1000;

const needImage = (app) => { if (!app.image) throw new Error("no image loaded — '/upload', '/blank' or '/open' first"); };

const isFormat = (tok) => Object.keys(constants.PAGE_SIZES).some((n) => n.toLowerCase() === tok.toLowerCase());

// `blank [fmt] [w h] [color]`: a page format or an explicit pixel pair, never both, then a colour.
export const parseBlank = (arg) => {
  const words = splitWords(arg);
  const out = { format: null, width: null, height: null, color: '#ffffff' };
  let i = 0;
  if (words[i] && isFormat(words[i])) out.format = normalizePageSize(words[i++]);
  if (words[i] && /^\d+$/.test(words[i])) {
    if (out.format || !/^\d+$/.test(words[i + 1] ?? '')) return null;
    out.width = Number(words[i]); out.height = Number(words[i + 1]); i += 2;
  }
  if (words[i]) out.color = words[i++];
  return i === words.length ? out : null;
};

export const editHandlers = ({ app, stencil }) => ({
  async upload(arg, host) {
    if (!arg) { host.pickFile(); return; }
    await stencil.load(arg);
    return `loaded ${arg}`;
  },
  async blank(arg) {
    const b = parseBlank(arg);
    if (!b) throw new Error("blank takes '[format] [w h] [color]' — e.g. '/blank 800 600 white' or '/blank b5 pink'");
    if (b.format) stencil.pageSize = b.format;
    await stencil.blank(b.color, b.width ? { size: { width: b.width, height: b.height } } : {});
    return 'blank page ready';
  },
  format(arg) {
    const words = splitWords(arg);
    if (!words.length) return [`page format: ${stencil.pageSize}`, `formats: ${Object.keys(constants.PAGE_SIZES).join(' ')} custom <w> <h>`];
    if (words[0].toLowerCase() === 'custom') {
      const [w, h] = [Number(words[1]), Number(words[2])];
      if (!(w > 0 && h > 0)) throw new Error("custom takes '<w> <h>' in cm — e.g. '/format custom 20 15'");
      stencil.apply({ pageSize: 'custom', pageWidth: w, pageHeight: h });
      return `page format: custom ${w}×${h}cm`;
    }
    stencil.pageSize = words[0];
    return `page format: ${stencil.pageSize}`;
  },
  async apply(arg) {
    needImage(app);
    if (!arg) throw new Error("apply needs a layout: '/apply <url>' or '/apply {\"lines\":[…]}'");
    let json = arg;
    if (!arg.trimStart().startsWith('{')) {
      const resp = await guardedFetch(arg);
      if (!resp.ok) throw new Error(`could not fetch ${arg} (HTTP ${resp.status})`);
      json = await readCapped(resp);
    }
    stencil.applyLayout(json);
    return 'layout drawn';
  },
  crop(arg) {
    needImage(app);
    const spec = parseCropSpec(arg);
    if (!spec || !Object.keys(spec).some((k) => k !== 'album')) throw new Error('crop takes edges — e.g. "/crop x1=10% x2=90% y1=10% y2=90%" (units: px, cm, mm, in, %)');
    stencil.crop(spec);
    return 'cropped';
  },
  rotate(arg) {
    needImage(app);
    const n = Number(arg);
    if (!Number.isInteger(n)) throw new Error("rotate takes a whole number of quarter turns — e.g. '/rotate 1', '/rotate -1'");
    const turns = ((n % 4) + 4) % 4;
    if (turns === 0) return 'no change (a full turn)';
    if (turns === 3) stencil.rotateLeft(); else for (let i = 0; i < turns; i++) stencil.rotateRight();
    return `rotated ${n * 90}°`;
  },
  flip() { needImage(app); stencil.flipImage(); return 'flipped'; },
  filter(arg) {
    needImage(app);
    const mode = String(arg).trim().toLowerCase();
    if (!mode) return `filter: ${stencil.filter}${stencil.filter === 'custom' ? ` ${stencil.filterColor}` : ''}`;
    if (FILTERS.includes(mode)) { stencil.filter = mode; return `filter: ${mode}`; }
    stencil.apply({ filter: 'custom', filterColor: arg.trim() });
    return `filter: tint ${stencil.filterColor}`;
  },
  async script(arg) {
    if (!arg) throw new Error("script takes .stc directives — e.g. '/script @crop 10%; @filter bw'");
    await stencil.execScript(arg.split(';').map((s) => s.trim()).filter(Boolean).join('\n'));
    return 'script ran';
  },
  undo() { stencil.undo(); return 'undone'; },
  redo() { stencil.redo(); return 'redone'; },
  reset() {
    let n = 0;
    while (app.history?.canUndo?.() && n < MAX_UNDO) { stencil.undo(); n++; }
    return n ? `reset: ${n} edit${n === 1 ? '' : 's'} undone` : 'nothing to undo (at the original)';
  },
  color(arg) {
    if (!arg) return `line colour: ${stencil.lineColor}`;
    stencil.lineColor = arg.trim();
    return `line colour: ${stencil.lineColor}`;
  },
  'point-color': (arg) => {
    if (!arg) return `point colour: ${stencil.pointColor || '(follows the line colour)'}`;
    stencil.pointColor = arg.trim().toLowerCase() === 'clear' ? '' : arg.trim();
    return `point colour: ${stencil.pointColor || '(follows the line colour)'}`;
  },
  'project-color': (arg) => {
    if (!arg) return `project colour: ${stencil.projectColor || '(default)'}`;
    stencil.projectColor = arg.trim().toLowerCase() === 'clear' ? '' : arg.trim();
    return `project colour: ${stencil.projectColor || '(default)'}`;
  },
  async save(arg) {
    needImage(app);
    const path = String(arg ?? '').trim();
    if (!path) { await stencil.save(); return 'saved'; }
    if (/\.stencil$/i.test(path)) { await stencil.saveProjectFile(); return 'project file saved'; }
    stencil.downloadImage();
    return 'picture saved';
  },
  layout() { needImage(app); stencil.downloadLayout(); return 'layout saved'; },
});
