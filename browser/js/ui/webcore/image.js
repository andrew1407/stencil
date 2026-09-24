// The picture the webcore word paints on an empty editor (config/webcore.json `image`): the
// sky's bands, the hill's dithered grass and the blocky clouds, one rect per same-colour run.
// Desktop twin: support/webcore/image.cpp, pixel for pixel.
import { WEBCORE, cellGrid, cellRuns, cloudBlocks } from './rules.js';

export const paintWebcoreImage = (ctx) => {
  const grid = cellGrid();
  const { cell } = grid;
  ctx.imageSmoothingEnabled = false;
  for (const r of [...cellRuns(grid), ...cloudBlocks()]) {
    ctx.fillStyle = r.color;
    ctx.fillRect(r.x * cell, r.y * cell, r.w * cell, r.h * cell);
  }
};

// The picture as a PNG blob, the way a blank page reaches the load path (core/image/blankImage.js).
export const webcoreImageBlob = () => new Promise((resolve, reject) => {
  const { width, height } = WEBCORE.image;
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  paintWebcoreImage(canvas.getContext('2d'));
  canvas.toBlob((blob) => (blob ? resolve(blob) : reject(new Error('webcore: the picture did not encode'))), 'image/png');
});
