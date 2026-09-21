// The Open-Image preview's arrival/departure cloud: the media is veiled while an overlay canvas
// assembles it in its own colours. Split out of ui/openImageModal.js; decoration only.
import { makeDustStage } from '../motion/dust/canvasDustStage.js';
import { runDust } from '../motion/dust/canvasDustDraw.js';
import { GHOST_MS } from '../motion/dust/canvasDustGrid.js';
import { dustEnabled } from '../motionPrefs.js';

export function createPreviewDust({ img, video, isVideo, sourceKey }) {
  // ghostIn IDENTICAL (motion/canvasFx.js). Plays ONCE per source ever seen this session.
  const animatedSources = new Set();
  // A departure ALWAYS has an arrival: the old picture blowing away and the new one
  // simply appearing is the asymmetry this flag closes (desktop twin: arrivalDue_).
  let arrivalDue = false;
  const willFly = () => {
    const key = sourceKey();
    return !!key && (arrivalDue || !animatedSources.has(key));
  };
  const preVeilIfNew = (el) => { if (willFly()) el.style.opacity = '0'; };

  let swapTimer = 0;
  // Decoration only: a taint or a zero-sized box must never leave the media veiled.
  let ghostStage = null, ghostTimer = 0;
  const dustOver = (el, size, gathering) => {
    if (!dustEnabled() || !size.width || !size.height) { el.style.opacity = ''; return; }
    try {
      const st = makeDustStage(el, size);
      if (!st) { el.style.opacity = ''; return; }
      ghostStage = st;
      runDust(st, GHOST_MS, gathering);
      ghostTimer = setTimeout(() => {
        ghostStage = null; ghostTimer = 0; el.style.opacity = '';
      }, GHOST_MS);
    } catch { el.style.opacity = ''; }
  };

  const gather = () => {
    if (!willFly()) return;
    animatedSources.add(sourceKey());
    arrivalDue = false;
    const asVideo = isVideo();
    const el = asVideo ? video() : img();
    const size = asVideo ? { width: video().videoWidth, height: video().videoHeight }
                         : { width: img().naturalWidth, height: img().naturalHeight };
    dustOver(el, size, true);
  };
  // The picture goes NOW and its cloud carries it out, so whatever lands next flies in too.
  const scatter = (el, size) => {
    el.style.opacity = '0';
    dustOver(el, size, false);
    arrivalDue = true;
  };
  // The arrival starts only once the departure has landed: run together, the two
  // clouds share one host and the second wipes the first off it.
  const queueArrival = (fn) => {
    clearTimeout(swapTimer);
    swapTimer = setTimeout(() => { swapTimer = 0; fn(); }, GHOST_MS);
  };

  // The stage is a canvas over the media, so the overlay's sweepDust never reaches it: left
  // running it played over the next tab's picture (desktop twin: cancelPreviewDust).
  const cancel = () => {
    if (ghostStage) { try { ghostStage.finish(); } catch { /* decoration */ } ghostStage = null; }
    clearTimeout(ghostTimer);
    clearTimeout(swapTimer);
    ghostTimer = swapTimer = 0;
    img().style.opacity = video().style.opacity = '';
  };
  const reset = () => { animatedSources.clear(); arrivalDue = false; };

  return { willFly, preVeilIfNew, gather, scatter, queueArrival, cancel, reset };
}
