// The wallpaper the aqua word paints on an empty editor (config/aqua.json `image`): the blue
// ramp and its swooshes, one rect per same-colour run. Desktop twin: support/aqua/wallpaper.cpp,
// pixel for pixel.
import { AQUA, wallGrid, wallRuns } from './rules.js';
import { paintedBlob } from '../skin/scene.js';

export const paintAquaImage = (ctx) => {
  const grid = wallGrid();
  const { cell } = grid;
  ctx.imageSmoothingEnabled = false;
  for (const r of wallRuns(grid)) {
    ctx.fillStyle = r.color;
    ctx.fillRect(r.x * cell, r.y * cell, r.w * cell, r.h * cell);
  }
};

export const aquaImageBlob = () => paintedBlob(AQUA.image, paintAquaImage, 'aqua');
