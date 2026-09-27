// The picture under the lines: one cached, filtered copy of the cropped/turned image per
// (image, filter, tint), which every frame composites and only those three rebuild. A duotone
// tint change of the picture on screen is painted in the image worker; a filter switch and
// export build the exact copy now.
import { ImageFilterCanvas, readFilterInput, isPixelFilter } from '../image/filterCanvas.js';
import { filterInWorker, imageWorkerUsable } from '../../worker/imageTasks.js';

const sameKey = (a, b) => !!a && !!b && a.image === b.image && a.filter === b.filter && a.color === b.color;

export class BaseLayer {
  #filters = new ImageFilterCanvas();
  #shown = null;    // { image, filter, source }: the base the last frame drew
  #wanted = null;   // the key the last frame asked for
  #job = null;      // the key the worker is painting
  #failed = null;   // a key the worker could not paint: built on this thread from then on

  // `onReady` repaints once a worker copy lands.
  constructor(onReady = () => {}) {
    this.onReady = onReady;
  }

  // On this thread, now: the export and thumbnail path. The image itself when unfiltered.
  now(image, filter, color) {
    return this.#filters.canvasFor(image, filter, color) || image;
  }

  // The exact copy when cached; while the worker paints another tint of the pixel filter already
  // on screen, the base that frame showed; else built now, exactly as now() builds it.
  frame(image, filter, color) {
    const key = { image, filter, color };
    this.#wanted = key;
    const hit = this.#filters.cached(image, filter, color);
    const shown = this.#shown;
    const retint = shown?.image === image && shown.filter === filter;
    if (!hit && isPixelFilter(filter) && retint && this.#submit(key)) return shown.source;
    const source = hit || this.now(image, filter, color);
    this.#shown = { image, filter, source };
    return source;
  }

  // True while the worker has, or will take, this key.
  #submit(key) {
    if (sameKey(this.#job, key)) return true;
    if (sameKey(this.#failed, key) || !imageWorkerUsable()) return false;
    if (this.#job) return true;
    let input;
    try {
      input = readFilterInput(key.image, key.filter, key.color);
    } catch {
      this.#failed = key;
      return false;
    }
    this.#job = key;
    filterInWorker(input).then((bitmap) => this.#land(key, bitmap), () => this.#land(key, null));
    return true;
  }

  // A tint the drag has already left still shows, so the colour tracks the drag on screen.
  #land(key, bitmap) {
    this.#job = null;
    const want = this.#wanted;
    if (!bitmap) this.#failed = key;
    else if (want && want.image === key.image && want.filter === key.filter) {
      if (want.color === key.color) this.#filters.adopt(key.image, key.filter, key.color, bitmap);
      this.#shown = { image: key.image, filter: key.filter, source: bitmap };
    } else bitmap.close?.();
    this.onReady();
  }
}
