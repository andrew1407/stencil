// One <img>/<video> pair per source tab of the Open-Image dialog, plus the events each pair
// answers. Part of ui/openImageTabs.js, which decides which pair is on screen.
import { retryWithoutCors } from '../mediaCors.js';

// What each pair holds: the source key being loaded, its kind, the object URL behind
// it, and a decode that landed while its tab was away (delivered on the way back).
const freshTab = () => ({ loading: null, isVideo: false, pending: false, objectUrl: null, src: '', pixelsReadable: true });

export function createMediaPairs({ previewImg, previewVideo, statusEl, tab, onDecode, onFrameBounds, onSeeked }) {
  // One pair per tab (desktop twin: TabPreviewCache), so a switch back re-decodes nothing.
  let img = previewImg, video = previewVideo;
  const media = { file: { img, video } };
  media.url = { img: img.cloneNode(false), video: video.cloneNode(false) };
  for (const el of [media.url.img, media.url.video]) { el.removeAttribute('id'); el.style.display = 'none'; }
  img.after(media.url.video, media.url.img);
  const tabState = { file: freshTab(), url: freshTab() };
  const st = () => tabState[tab()] || tabState.file;

  // The crop rides whichever media is on show — for a video that is the PLAYER, so the
  // rect is drawn on the picture already there instead of on a second copy of it.
  const cropMedia = () => (st().isVideo ? video : img);
  const mediaPixels = () => (st().isVideo
    ? { width: video.videoWidth, height: video.videoHeight }
    : { width: img.naturalWidth, height: img.naturalHeight });
  const revoke = (s) => { if (s.objectUrl) { URL.revokeObjectURL(s.objectUrl); s.objectUrl = null; } };

  // Each pair's own events. Only the ACTIVE tab's decode lands now; one arriving while
  // its tab is away waits (pending) and lands when the tab is shown again.
  const wireMedia = (name) => {
    const s = tabState[name], pair = media[name];
    const landed = (el) => { if (name === tab()) onDecode(el, mediaPixels()); else s.pending = true; };
    pair.img.addEventListener('load', () => { if (!s.isVideo) landed(pair.img); });
    // An undecoded <video> reports 300x150, so the rect waits for its real frame size.
    pair.video.addEventListener('loadeddata', () => {
      if (!s.isVideo) return;
      if (name === tab()) onFrameBounds();
      landed(pair.video);
    });
    // A host that refuses the CORS ask fails the load — come back for the pixels-less one.
    const failed = (el) => {
      if (retryWithoutCors(el, s.src)) return;
      if (name === tab()) statusEl.textContent = 'Could not load that source.';
    };
    pair.img.addEventListener('error', () => { if (!s.isVideo) failed(pair.img); });
    pair.video.addEventListener('error', () => { if (s.isVideo) failed(pair.video); });
    pair.video.addEventListener('seeked', () => onSeeked(pair.video));
  };
  wireMedia('file');
  wireMedia('url');

  const swapTo = (name) => {
    img.style.display = video.style.display = 'none';   // the outgoing pair
    ({ img, video } = media[name]);
  };
  // Full reset, so a prior open's image never leaks in.
  const resetAll = () => {
    for (const t of ['file', 'url']) {
      revoke(tabState[t]);
      Object.assign(tabState[t], freshTab());
      media[t].img.removeAttribute('src');
      media[t].img.style.opacity = media[t].video.style.opacity = '';
      media[t].video.pause();
      media[t].video.removeAttribute('src');
    }
  };
  const pauseAll = () => {
    for (const t of ['file', 'url']) { revoke(tabState[t]); media[t].video.pause(); }
  };

  return { pairFor: (name) => media[name], st, img: () => img, video: () => video,
           cropMedia, mediaPixels, revoke, swapTo, resetAll, pauseAll };
}
