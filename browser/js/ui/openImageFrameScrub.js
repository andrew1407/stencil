// The Open-Image dialog's video Frame row: the frame field, the scrub bar under the picture
// and the settled seek they share. Split out of ui/openImageModal.js.
import { FRAME_INDEX_FPS } from '../core/videoFrame.js';

// Desktop twin: OpenImageDialog's fetchTimer_ interval — one seek per settled drag.
const SEEK_SETTLE_MS = 80;

export function createFrameScrub({ frameEl, scrubEl, video, cropMedia, isVideoSource }) {
  // The field counts FRAMES (desktop's Frame row); every consumer below wants seconds.
  const frameSeconds = () => (Number(frameEl && frameEl.value) || 0) / FRAME_INDEX_FPS;
  const lastFrame = () => Math.max(0, Math.round((video().duration || 0) * FRAME_INDEX_FPS) - 1);
  const syncFrameBounds = () => { frameEl.max = scrubEl.max = String(lastFrame()); };
  // The bar shows how far in the frame sits, and spans exactly the picture above it.
  const syncScrub = () => {
    const n = Number(frameEl.value) || 0;
    scrubEl.value = String(n);
    const max = Number(scrubEl.max) || 0;
    scrubEl.style.setProperty('--scrub-fill', `${max > 0 ? (n / max) * 100 : 0}%`);
    const w = Math.round(cropMedia().getBoundingClientRect().width);
    if (w) scrubEl.style.width = `${w}px`;
  };

  // ONE seek path for the field and the bar, so neither has to fake an event at the other.
  // Dragging fires per pixel, so the seek waits for the drag to settle (desktop: fetchTimer_).
  let seekTimer = 0;
  const seekToFrame = (n) => {
    frameEl.value = String(n);
    syncScrub();
    if (!isVideoSource() || !video().readyState) return;
    clearTimeout(seekTimer);
    seekTimer = setTimeout(() => {
      seekTimer = 0;
      try { video().currentTime = n / FRAME_INDEX_FPS; } catch { /* out-of-range seek */ }
    }, SEEK_SETTLE_MS);
  };
  frameEl.addEventListener('input', () => seekToFrame(Number(frameEl.value) || 0));
  scrubEl.addEventListener('input', () => seekToFrame(Number(scrubEl.value) || 0));

  // A seek that has landed names the frame on screen — unless a newer one is already
  // on its way, in which case this would rewind the field the user just set.
  const showLandedFrame = (el) => {
    if (el !== video() || seekTimer) return;
    frameEl.value = String(Math.round(el.currentTime * FRAME_INDEX_FPS));
    syncScrub();
  };

  return { frameSeconds, syncFrameBounds, syncScrub, showLandedFrame };
}
