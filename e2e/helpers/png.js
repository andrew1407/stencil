// A solid PNG of any size, built here so a spec can hand the app a REAL picture through a
// file input (fixtures/pixel.png is 1x1 — too small for a preview, a crop or a dust stage).
// Deflate + CRC come from Node's own zlib; nothing is installed for this.
import zlib from 'node:zlib';

const crc32 = (buf) => {
  let c = ~0;
  for (const b of buf) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
};

const chunk = (type, body) => {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(body.length);
  const head = Buffer.concat([Buffer.from(type, 'ascii'), body]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(head));
  return Buffer.concat([len, head, crc]);
};

/** A w*h 8-bit truecolour PNG, one colour throughout. */
export function solidPng(w, h, rgb = [0x30, 0x80, 0xc0]) {
  const stride = w * 3 + 1;                 // one filter byte per scanline
  const raw = Buffer.alloc(stride * h);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) raw.set(rgb, y * stride + 1 + x * 3);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr.set([8, 2, 0, 0, 0], 8);
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
                        chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)),
                        chunk('IEND', Buffer.alloc(0))]);
}

/** Ready for setInputFiles. */
export const pngFile = (w, h, name = 'picture.png') =>
  ({ name, mimeType: 'image/png', buffer: solidPng(w, h) });
