// The picture the webcore word paints on an empty editor (config/webcore.json `image`): the
// sky's bands, the hill's dithered grass and the blocky clouds, one rect per same-colour run.
// Desktop twin: support/webcore/image.cpp, pixel for pixel.
import { WEBCORE, cellGrid, cellRuns, cloudBlocks } from './rules.js';
import { paintedBlob } from '../skin/scene.js';

export const paintWebcoreImage = (ctx) => {
  const grid = cellGrid();
  const { cell } = grid;
  ctx.imageSmoothingEnabled = false;
  for (const r of [...cellRuns(grid), ...cloudBlocks()]) {
    ctx.fillStyle = r.color;
    ctx.fillRect(r.x * cell, r.y * cell, r.w * cell, r.h * cell);
  }
};

export const webcoreImageBlob = () => paintedBlob(WEBCORE.image, paintWebcoreImage, 'webcore');
