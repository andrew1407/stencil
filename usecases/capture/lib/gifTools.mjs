// GIF assembly through the system ffmpeg: palettegen + paletteuse keeps files small, and
// flat UI compresses best undithered on a small palette with rectangle frame diffs.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const FRAME_PATTERN = 'frame-%04d.png';

const filterChain = (fps, width, { colors = 128, scale = 'lanczos' } = {}) =>
  `fps=${fps},scale=${width}:-1:flags=${scale},split[a][b];[a]palettegen=stats_mode=diff:max_colors=${colors}[p];`
  + '[b][p]paletteuse=dither=none:diff_mode=rectangle';

const runFfmpeg = (args) =>
  execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...args], { stdio: 'inherit' });

// A Playwright .webm → .gif, optionally trimmed to [start, end] seconds.
export function webmToGif(src, dst, { fps = 10, width = 800, start, end, ...look } = {}) {
  const trim = [];
  if (start != null) trim.push('-ss', String(start));
  if (end != null) trim.push('-to', String(end));
  runFfmpeg([...trim, '-i', src, '-vf', filterChain(fps, width, look), dst]);
  return dst;
}

// A numbered PNG sequence (frame-0001.png …) → .gif. `inFps` is the rate the frames were
// taken at; `fps` the rate kept in the GIF.
export function framesToGif(dir, dst, { inFps = 15, fps = 10, width = 800, pattern = FRAME_PATTERN, ...look } = {}) {
  runFfmpeg(['-framerate', String(inFps), '-i', path.join(dir, pattern), '-vf', filterChain(fps, width, look), dst]);
  return dst;
}

// Re-encode a PNG on a 256-colour palette: terminals and flat UI lose nothing and shrink
// several times over.
export function quantizePng(file) {
  const tmp = `${file}.q.png`;
  runFfmpeg(['-i', file, '-vf', 'split[a][b];[a]palettegen=max_colors=256[p];[b][p]paletteuse=dither=none', tmp]);
  fs.renameSync(tmp, file);
  return file;
}
