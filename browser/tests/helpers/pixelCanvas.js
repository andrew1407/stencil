// A pixel-true stand-in for OffscreenCanvas + ImageData: drawImage copies RGBA (through a stand-in
// CSS grayscale), get/putImageData copy the buffer and transferToImageBitmap hands the pixels over,
// so a test compares what two paint paths PRODUCE, byte for byte.
export class PixelImageData {
  constructor(data, width, height) { this.data = data; this.width = width; this.height = height; }
}

const gray = (d) => {
  for (let i = 0; i < d.length; i += 4) d[i] = d[i + 1] = d[i + 2] = Math.round(0.2126 * d[i] + 0.7152 * d[i + 1] + 0.0722 * d[i + 2]);
};

export class PixelCanvas {
  constructor(width = 0, height = 0) { this.width = width; this.height = height; this.px = null; }

  #buffer() {
    if (!this.px || this.px.length !== this.width * this.height * 4) this.px = new Uint8ClampedArray(this.width * this.height * 4);
    return this.px;
  }

  getContext() {
    const canvas = this;
    return {
      canvas, filter: 'none', fillStyle: '#000000',
      // The whole buffer in one opaque #rrggbb: the tests fill whole canvases only.
      fillRect() {
        const n = parseInt(String(this.fillStyle).slice(1), 16);
        const d = canvas.#buffer();
        for (let i = 0; i < d.length; i += 4) d.set([n >> 16, (n >> 8) & 255, n & 255, 255], i);
      },
      drawImage(src) {
        const d = Uint8ClampedArray.from(src.px);
        if (this.filter === 'grayscale(100%)') gray(d);
        canvas.#buffer().set(d);
      },
      getImageData: (x, y, w, h) => new PixelImageData(Uint8ClampedArray.from(canvas.#buffer()), w, h),
      putImageData: (image) => { canvas.#buffer().set(image.data); },
    };
  }

  transferToImageBitmap() {
    const bitmap = { width: this.width, height: this.height, px: Uint8ClampedArray.from(this.#buffer()), close() {} };
    this.px = null;
    return bitmap;
  }
}

// A w×h picture whose every channel varies, alpha included.
export const gradientImage = (width, height) => {
  const px = new Uint8ClampedArray(width * height * 4);
  for (let i = 0; i < width * height; i++) {
    const x = i % width, y = Math.floor(i / width);
    px.set([(x * 37 + y * 11) % 256, (x * 5 + y * 53) % 256, (x * y * 7) % 256, 128 + ((x + y) % 128)], i * 4);
  }
  return { width, height, px };
};
