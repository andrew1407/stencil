// The accent recolour of the CLI stage, cli/src/console/render/logoFx/press.zig wipeRecolor over
// the DOM: the banner's cells and the accented scrollback take the new accent left to right, the
// wordmark letter by letter, the rule ahead of the seam. A restyled skin wears no accent of its
// own, so under one the new colour lands at once.
import { motionReduced, consoleSpeed, CONSOLE_SPEED } from '../motion/motionPrefs.js';
import { logoBannerLayout } from '../../core/format/logoBanner.js';
import { traitsOf } from './skin/table.js';
import { restyles } from './skin/restyle.js';
import { wordmarkOf } from './banner.js';
import { runWipe, wipeFrame, ruleReach } from './wipe.js';

const LAYOUT = logoBannerLayout(false);
const HEADER_ROWS = LAYOUT.frameHeight + 2 + 2 * LAYOUT.curveRows;

// `paint(accent)` repaints the banner in that accent; `skin()` is the skin worn now.
export const createRecolour = ({ host, banner, out, prompt, rules, skin, paint, schedule = (fn, ms) => setTimeout(fn, ms) }) => {
  let painted = null, wiping = false, cancel = () => {};
  const wipeTo = (next) => {
    cancel();
    const old = painted;
    if (restyles(skin())) { painted = next; paint(next); return; }
    paint(old);   // the press frame gives way; the wash works over the full banner
    const rows = [...banner.children];
    const cols = Math.max(...rows.map((r) => r.children.length), 1);
    const letters = [...wordmarkOf(skin())].filter((ch) => ch !== ' ').length;
    // Every run in the accent takes the seam: the echoes' prompt and word, the help heads, the
    // sections and the prompt glyph (press.zig accentReach / paintRecolored).
    const accented = [...out.querySelectorAll('.console-line-cmd, .console-line-section'), prompt];
    const reach = Math.max(cols, ...accented.map((el) => el.textContent.length));
    const cellsOf = (row) => [...(rows[row - 1]?.querySelectorAll('.cli-accent') ?? [])];
    const step = {
      wash(x) {
        for (let r = 1; r <= HEADER_ROWS; r++) cellsOf(r).forEach((cell) => { cell.style.color = old; });
        for (const [r, c0, c1] of wipeFrame(HEADER_ROWS, cols, x, reach)) {
          for (const cell of cellsOf(r)) { const c = [...rows[r - 1].children].indexOf(cell); if (c >= c0 && c < c1) cell.style.color = next; }
        }
        accented.forEach((el) => el.style.setProperty('--seam', `${x}ch`));
        rules.forEach((r) => r.style.setProperty('--rule-reach', `${ruleReach(100, x, reach)}%`));
      },
      // A plain wordmark lights one letter at a time, the one before going plain again; an
      // accented one turns letter by letter and stays (press.zig drawWordmark).
      letter(i) {
        const lit = [...banner.querySelectorAll('.cli-letter')].filter((l) => l.textContent !== ' ');
        const stays = !!traitsOf(skin())?.wordmarkAccented;
        lit.forEach((l, j) => { l.style.color = stays ? (j <= i ? next : old) : (j === i ? next : ''); });
      },
      settle() {
        wiping = false;
        painted = next;
        accented.forEach((el) => el.style.removeProperty('--seam'));
        rules.forEach((r) => r.style.removeProperty('--rule-reach'));
        paint(next);
      },
    };
    host.style.setProperty('--cli-old', old);
    host.style.setProperty('--cli-new', next);
    wiping = true;
    // The wipe runs at the Zig console's own tempo (press.zig); speed 1 lands it at once.
    cancel = runWipe({ reach, letters, paint: step, schedule, instant: motionReduced() || consoleSpeed() >= CONSOLE_SPEED.max });
  };
  return {
    wipeTo,
    cancel: () => cancel(),
    wiping: () => wiping,
    painted: () => painted,
    setPainted(accent) { painted = accent; },
  };
};
