// The injected scanner's work caps (lib/image/scan.js): on a huge page it reads the computed
// styles of at most MAX_SCAN_ELEMENTS elements and encodes at most MAX_VIDEO_FRAMES video frames.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scanPageForImages, MAX_SCAN_ELEMENTS, MAX_VIDEO_FRAMES } from '../../../src/lib/image/scan.js';
import { installDom, stubDoc, stubEl } from '../../helpers/domStub.js';

const playingVideo = (i) => Object.assign(stubEl('video'), {
  videoWidth: 640, videoHeight: 360, readyState: 4, paused: false, currentTime: 1,
  currentSrc: `https://cdn.example/v${i}.mp4`, poster: '',
});

const scanStubPage = async ({ elements = 0, videos = 0 } = {}) => {
  let styleReads = 0;
  let encoded = 0;
  const all = Array.from({ length: elements }, () => stubEl('div'));
  const vids = Array.from({ length: videos }, (_, i) => playingVideo(i));
  const canvas = () => Object.assign(stubEl('canvas'), {
    getContext: () => ({ drawImage() {} }),
    toDataURL: () => `data:image/jpeg;base64,${++encoded}`,
  });
  const restore = installDom({
    document: stubDoc({ createElement: (t) => (t === 'canvas' ? canvas() : stubEl(t)) }),
    location: new URL('https://page.example/'),
    getComputedStyle: () => { styleReads += 1; return {}; },
  });
  document.querySelectorAll = (sel) => (sel === '*' ? all : sel === 'video' ? vids : []);
  try {
    return { items: await scanPageForImages(1000), styleReads, encoded };
  } finally { restore(); }
};

test('a page with more elements than the cap reads styles of only the first MAX_SCAN_ELEMENTS', async () => {
  const { styleReads } = await scanStubPage({ elements: MAX_SCAN_ELEMENTS + 5000 });
  assert.equal(styleReads, MAX_SCAN_ELEMENTS * 3, 'element, ::before, ::after');
  assert.equal((await scanStubPage({ elements: 10 })).styleReads, 30, 'a small page is read whole');
});

test('past MAX_VIDEO_FRAMES playing videos, the rest still list but without an encoded frame', async () => {
  const { items, encoded } = await scanStubPage({ videos: MAX_VIDEO_FRAMES + 4 });
  assert.equal(encoded, MAX_VIDEO_FRAMES);
  const videos = items.filter((i) => i.kind === 'video');
  assert.equal(videos.length, MAX_VIDEO_FRAMES + 4);
  assert.equal(videos.filter((v) => v.hasFrame).length, MAX_VIDEO_FRAMES);
  assert.ok(videos.filter((v) => !v.hasFrame).every((v) => v.videoUrl.startsWith('https://cdn.example/')));
});
