// The clip the video shots open. Made here with ffmpeg rather than committed, so no binary
// rides in git. testsrc2 carries a moving ball and a running timecode, so a picked FRAME
// reads as one moment of a clip rather than a flat picture.
//
// It is written under a NEUTRAL root, not the scratch dir: the desktop dialog shows the
// chosen file's full path, and the scratch dir sits inside the checkout — under someone's
// home directory. Same rule, and the same roots, as the VS Code captures.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

// Small enough that the preview leaves the scrub bar and the Frame row above the fold.
export const CLIP = Object.freeze({ seconds: 4, fps: 12, size: '480x270', name: 'sample.mp4' });

const ROOTS = Object.freeze({ win32: 'C:\\Temp\\stencil-capture', default: '/tmp/stencil-capture' });

// Returns { dir, file, name }; the directory is what the media server serves.
export function sampleVideo(spec = CLIP) {
  const dir = path.join(ROOTS[os.platform()] || ROOTS.default, 'media');
  fs.mkdirSync(dir, { recursive: true });
  const file = path.join(dir, spec.name);
  // H.264 in MP4 with yuv420p: the one encoding Chromium and Qt Multimedia both open.
  execFileSync('ffmpeg', ['-y', '-hide_banner', '-loglevel', 'error',
    '-f', 'lavfi', '-i', `testsrc2=size=${spec.size}:rate=${spec.fps}:duration=${spec.seconds}`,
    '-pix_fmt', 'yuv420p', '-c:v', 'libx264', '-preset', 'veryfast', file]);
  if (!fs.existsSync(file)) throw new Error(`ffmpeg wrote no clip at ${file}`);
  return { dir, file, name: spec.name };
}
