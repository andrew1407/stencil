// The matrix skin's entrance, a port of cli/src/console/render/logoFx/rain.zig: green digit rain
// down the scrollback rows for `frames` frames, the lines hidden under it; a keystroke cuts it short.
import FX from '../../../../../common/config/console/fx.json' with { type: 'json' };

const R = FX.rain;

// The body as a cell grid, measured off the scrollback's own font with a probe row of ten M's,
// so the rain reaches the rule as the terminal's does; the guesses stand where nothing lays out.
export const gridOf = (pre) => {
  const doc = pre.ownerDocument || globalThis.document;
  const probe = doc.createElement('span');
  probe.className = 'cli-rain-row';
  probe.textContent = 'MMMMMMMMMM';
  pre.appendChild(probe);
  const box = probe.getBoundingClientRect?.() ?? {};
  pre.removeChild(probe);
  const cs = doc.defaultView?.getComputedStyle?.(pre);
  const padX = cs ? parseFloat(cs.paddingLeft) + parseFloat(cs.paddingRight) : 0;
  const padY = cs ? parseFloat(cs.paddingTop) + parseFloat(cs.paddingBottom) : 0;
  const cw = (box.width || 80) / 10, ch = box.height || 19;
  return { cols: Math.max(20, Math.floor(((pre.clientWidth || 0) - padX) / cw) || 80), rows: Math.max(0, Math.floor(((pre.clientHeight || 0) - padY) / ch) || 20) };
};

// One frame's drops: head row per column, advanced; a drop past the bottom starts afresh.
export const stepHeads = (heads, h, random) => heads.map((head) => {
  const next = head + 1;
  return next - R.trail > h ? -Math.floor(random() * Math.max(1, h)) : next;
});

export const rain = (pre, { cols, rows, random = Math.random, schedule = (fn, ms) => setTimeout(fn, ms), onDone = () => {} }) => {
  if (rows <= 0 || cols <= 0) { onDone(); return () => {}; }
  const doc = pre.ownerDocument;
  const grid = Array.from({ length: rows }, () => Array(cols).fill(' '));
  let heads = Array.from({ length: cols }, () => -Math.floor(random() * (rows + R.trail)));
  let f = 0, stopped = false, timer = null;
  const sweep = () => { for (const el of [...pre.querySelectorAll('.cli-rain-row')]) el.remove(); };
  const paint = () => {
    sweep();
    grid.forEach((row) => { const line = doc.createElement('span'); line.className = 'cli-rain-row'; line.textContent = `${row.join('')}\n`; pre.appendChild(line); });
  };
  const frame = () => {
    if (stopped) return;
    heads = stepHeads(heads, rows, random);
    heads.forEach((r, c) => {
      if (r >= 0 && r < rows) grid[r][c] = random() < 0.5 ? '1' : '0';
      if (r - R.trail >= 0 && r - R.trail < rows) grid[r - R.trail][c] = ' ';
    });
    paint();
    if (++f < R.frames) timer = schedule(frame, R.frameMs); else stop();
  };
  const stop = () => { if (stopped) return; stopped = true; if (timer) clearTimeout(timer); sweep(); delete pre.dataset.raining; onDone(); };
  pre.dataset.raining = '1';
  timer = schedule(frame, R.frameMs);
  return stop;
};
