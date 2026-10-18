// The pinned logo of the CLI stage: the core's banner rows (core/format/logoBanner) painted
// cell by cell — borders in the accent, the mark in the annotation yellow, the wordmark beside
// the middle row — so a wipe or a skin can recolour any column.
import { logoBannerRows, logoBannerLayout } from '../../core/format/logoBanner.js';
import { traitsOf, hueAt, GOLD } from './skin/table.js';
import { pictureAt, picture, catPose } from './skin/pictures.js';

const BORDER = new Set(['╭', '╮', '╰', '╯', '╱', '╲', '│', '─']);
// A fullwidth form (the cat's ｀) takes two cells, as a terminal gives it; the cell after it is empty.
const wide = (ch) => ch >= '\uFF01' && ch <= '\uFF60';
const MARK_YELLOW = '#ffff00';
export const WORDMARK = 'S T E N C I L';

export const markOf = (skin) => traitsOf(skin)?.mark ?? 'S';
export const wordmarkOf = (skin) => traitsOf(skin)?.wordmark ?? WORDMARK;

// One cell: its glyph and its role — 'accent' border, 'mark' art, 'art' the frame's own ground, 'panel' the
// margin around it, 'blank' the indent — by column in the banner's layout.
export const bannerCells = (skin, compact, clock = null) => {
  const l = logoBannerLayout(compact);
  const rows = logoBannerRows(markOf(skin), compact);
  const t = traitsOf(skin);
  const artStart = l.indent + 1 + l.margin, artEnd = artStart + l.frameWidth;
  const bodyTop = 1 + l.curveRows;
  return rows.map((row, r) => {
    // The panel's own rule rows run through the art columns and stay the border's.
    const rule = r === 0 || r === rows.length - 1;
    const first = row.search(/\S/);
    const cells = [...row].map((ch, c) => ({ ch, role: c < first ? 'blank' : BORDER.has(ch) && (rule || c < artStart || c >= artEnd) ? 'accent' : (c >= artStart && c < artEnd) ? (ch !== ' ' ? 'mark' : 'art') : 'panel', c }));
    // The meow skin walks its cat across the frame rows in place of the mark.
    if (t?.movesLogo && clock && r >= bodyTop && r < bodyTop + l.frameHeight) {
      const pose = catPose(l.frameWidth, l.frameHeight, clock);
      for (let c = artStart; c < artEnd; c++) cells[c] = { ch: ' ', role: 'art', c };
      if (r - bodyTop === pose.row) {
        let c = artStart + pose.col;
        for (const ch of pose.text) {
          if (c >= artEnd) break;
          cells[c] = { ch, role: 'mark', c, wide: wide(ch) };
          c += 1;
          if (wide(ch) && c < artEnd) cells[c++] = { ch: '', role: 'art', c: c - 1 };
        }
      }
    }
    return cells;
  });
};

// The wordmark's letters, each a picture under a letter-swapping skin, in the row's accent or plain.
export const wordmarkCells = (skin, clock = null) => {
  const t = traitsOf(skin);
  let prev = null;
  return [...wordmarkOf(skin)].map((ch, i) => {
    if (ch === ' ' || !t?.replacesLetters || !clock || skin === 'pie') return { ch, role: 'letter', c: i };   // the pie's letters stay, in gold
    const idx = pictureAt(skin, 7, i, prev, clock);
    prev = idx;
    return { ch: picture(skin, idx), role: 'picture', c: i };
  });
};

// Paint the cells into `pre`: a span per cell, coloured for the accent (`accent` is what the skin
// makes of it, skin/restyle.js accentUnder), the mark and the skin. The press frame keeps the full
// logo's box: the smaller icon shrinks towards its middle, the wordmark stays put (press.zig).
export const paintBanner = (pre, { skin = null, compact = false, accent, clock = null, frame = 0 } = {}) => {
  const doc = pre.ownerDocument || globalThis.document;
  const t = traitsOf(skin);
  const cells = bannerCells(skin, compact, clock);
  const box = compact ? bannerCells(skin, false) : cells;
  const top = (box.length - cells.length) >> 1;
  const width = Math.max(...box.map((r) => r.length));
  const label = logoBannerLayout(false).labelRow + 1 + logoBannerLayout(false).curveRows;
  while (pre.firstChild) pre.removeChild(pre.firstChild);
  box.forEach((_, r) => {
    const drawn = r >= top ? cells[r - top] ?? [] : [];
    const row = Array.from({ length: width }, (__, c) => drawn[c] ?? { ch: ' ', role: 'blank', c });
    const line = doc.createElement('span');
    line.className = 'cli-banner-row';
    for (const cell of row) {
      const s = doc.createElement('span');
      s.textContent = cell.ch;
      s.className = `cli-cell cli-${cell.role}${cell.wide ? ' cli-wide' : ''}`;
      if (cell.role === 'accent') s.style.color = t?.paintsRuns ? hueAt(skin, cell.c, frame) : accent;
      if (cell.role === 'mark') s.style.color = t?.paintsRuns ? hueAt(skin, cell.c, frame) : t?.paintsCells ? t.fg : (t?.markColor ?? MARK_YELLOW);
      line.appendChild(s);
    }
    if (r === label) {
      line.appendChild(doc.createTextNode('   '));
      const mark = doc.createElement('b');
      mark.className = `cli-wordmark${t?.title ? ' cli-wordmark-title' : ''}`;
      for (const cell of wordmarkCells(skin, clock)) {
        const s = doc.createElement('span');
        s.textContent = cell.ch;
        s.className = `cli-letter cli-${cell.role}`;
        if (skin === 'pie') s.style.color = GOLD;
        else if (t?.wordmarkAccented) s.style.color = t?.paintsRuns ? hueAt(skin, width + 3 + cell.c, frame) : accent;   // its screen column: three past the box
        mark.appendChild(s);
      }
      line.appendChild(mark);
    }
    pre.appendChild(line);
  });
};
