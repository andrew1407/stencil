// GIF assembly through the system ffmpeg: palettegen + paletteuse keeps files small, and
// flat UI compresses best undithered on a small palette with rectangle frame diffs. Every GIF
// opens on `firstMs` of its first picture and rests `lastMs` on its last, whatever its source.
import { execFileSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const FRAME = /^frame-\d+\.png$/;

// `cropPx` ({ x, y, width, height } in frame pixels) keeps only the region the clip is about.
const filterChain = (fps, width, { colors = 128, scale = 'lanczos', cropPx = null } = {}) =>
  `fps=${fps},${cropPx ? `crop=${cropPx.width}:${cropPx.height}:${cropPx.x}:${cropPx.y},` : ''}`
  + `scale=${width}:-1:flags=${scale},split[a][b];[a]palettegen=stats_mode=diff:max_colors=${colors}[p];`
  + '[b][p]paletteuse=dither=none:diff_mode=rectangle';

const runFfmpeg = (args) =>
  execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...args], { stdio: 'inherit' });

const digest = (file) => crypto.createHash('sha1').update(fs.readFileSync(file)).digest('hex');

// Identical neighbours become one picture shown for their summed time; then the first and last
// pictures are set to firstMs and lastMs, so a still start or end never runs long or short.
const paced = (timeline, firstMs, lastMs) => {
  const runs = [];
  for (const { file, seconds } of timeline) {
    const hash = digest(file);
    const last = runs[runs.length - 1];
    if (last && last.hash === hash) last.seconds += seconds;
    else runs.push({ file, hash, seconds });
  }
  if (runs.length === 1) runs[0].seconds = (firstMs + lastMs) / 1000;
  else {
    runs[0].seconds = firstMs / 1000;
    runs[runs.length - 1].seconds = lastMs / 1000;
  }
  return runs;
};

// [{ file, seconds }] as an ffconcat list. The demuxer gives the final entry the previous
// entry's duration, so the last picture is listed twice at half its time.
const writeConcat = (list, runs) => {
  const lines = ['ffconcat version 1.0'];
  for (const run of runs.slice(0, -1)) lines.push(`file '${run.file}'`, `duration ${run.seconds.toFixed(4)}`);
  const tail = runs[runs.length - 1];
  lines.push(`file '${tail.file}'`, `duration ${(tail.seconds / 2).toFixed(4)}`, `file '${tail.file}'`);
  fs.writeFileSync(list, `${lines.join('\n')}\n`);
  return list;
};

// [{ file, seconds }] in play order → one GIF, through ffmpeg's concat demuxer.
function timelineToGif(timeline, dst, { fps = 10, width = 800, firstMs = 500, lastMs = 1800, ...look } = {}) {
  if (!timeline.length) throw new Error(`no frames for ${path.basename(dst)}`);
  const list = writeConcat(`${dst}.ffconcat`, paced(timeline, firstMs, lastMs));
  try {
    runFfmpeg(['-f', 'concat', '-safe', '0', '-i', list, '-vf', filterChain(fps, width, look), dst]);
  } finally {
    fs.rmSync(list, { force: true });
  }
  return dst;
}

// A numbered PNG strip (frame-0001.png …) taken every 1/inFps seconds → one GIF.
export function framesToGif(dir, dst, { inFps = 15, ...look } = {}) {
  const files = fs.readdirSync(dir).filter((f) => FRAME.test(f)).sort();
  return timelineToGif(files.map((f) => ({ file: path.join(dir, f), seconds: 1 / inFps })), dst, look);
}

// A screencast's frames from `from` to `end`, each lasting until the next one was painted (the
// one showing at `from` starts there).
const castTimeline = ({ frames, end }, from = frames[0]?.at ?? end) => frames
  .map((frame, i) => ({ file: frame.file, at: Math.max(frame.at, from), until: i + 1 < frames.length ? frames[i + 1].at : end }))
  .filter((f) => f.until > from)
  .map((f) => ({ file: f.file, seconds: Math.max(0.001, f.until - f.at) }));

// A screencast (lib/shot/cast.mjs) → one GIF.
export const castToGif = (cast, dst, look = {}) => timelineToGif(castTimeline(cast), dst, look);

// Two screencasts filmed at once → one GIF, side by side on a shared clock: `left` and `right`
// each hold their last frame until the later one ends, then both rest `lastMs` more; `cropPx`
// trims each side before they meet.
export function pairToGif(left, right, dst, { fps = 10, width = 800, gap = 8, cropPx = null, lastMs = 1800, ...look } = {}) {
  const side = cropPx ? `,crop=${cropPx.width}:${cropPx.height}:${cropPx.x}:${cropPx.y}` : '';
  const from = Math.max(left.frames[0].at, right.frames[0].at);
  const end = Math.max(left.end, right.end) + lastMs / 1000;
  const lists = [left, right].map((cast, i) => writeConcat(`${dst}.${i}.ffconcat`, castTimeline({ ...cast, end }, from)));
  try {
    runFfmpeg(['-f', 'concat', '-safe', '0', '-i', lists[0], '-f', 'concat', '-safe', '0', '-i', lists[1],
      '-filter_complex', `[0:v]fps=${fps}${side},pad=iw+${gap}:ih:0:0:color=0x888888[l];[1:v]fps=${fps}${side}[r];`
        + `[l][r]hstack=inputs=2,${filterChain(fps, width, look)}`, dst]);
  } finally {
    for (const list of lists) fs.rmSync(list, { force: true });
  }
  return dst;
}

// Cut a horizontal band out of a PNG and close the gap. `top` and `bottom` are fractions of
// the height: everything above `top` and below `bottom` is kept, stacked.
export function dropBand(file, top, bottom) {
  const tmp = `${file}.b.png`;
  runFfmpeg(['-i', file, '-filter_complex',
    `[0:v]crop=iw:round(ih*${top}):0:0[a];`
    + `[0:v]crop=iw:ih-round(ih*${bottom}):0:round(ih*${bottom})[b];`
    + '[a][b]vstack=inputs=2', tmp]);
  fs.renameSync(tmp, file);
  return file;
}

// Re-encode a PNG on a 256-colour palette: terminals and flat UI lose nothing and shrink
// several times over.
export function quantizePng(file) {
  const tmp = `${file}.q.png`;
  runFfmpeg(['-i', file, '-vf', 'split[a][b];[a]palettegen=max_colors=256[p];[b][p]paletteuse=dither=none', tmp]);
  fs.renameSync(tmp, file);
  return file;
}
