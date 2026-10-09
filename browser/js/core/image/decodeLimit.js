// The decoded-pixel cap every load holds a picture to before it is drawn (LIMITS.imageMaxSide and
// imageMaxPixels, the CLI decoder's STBI_MAX_DIMENSIONS), and the 2D context a canvas the
// browser refuses to allocate fails with by name, not as a TypeError mid-load.
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { imageMaxSide, imageMaxPixels } = constants.LIMITS;

export const IMAGE_TOO_LARGE =
  `Image is too large to open (over ${imageMaxSide} px a side or ${Math.round(imageMaxPixels / 1e6)} MP)`;
export const CANVAS_UNAVAILABLE = 'This browser cannot allocate a canvas that large';

export const imageTooLarge = (width, height) =>
  width > imageMaxSide || height > imageMaxSide || width * height > imageMaxPixels;

export const context2d = (canvas) => {
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error(CANVAS_UNAVAILABLE);
  return ctx;
};
