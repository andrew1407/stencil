// @use stencil
// A single-source script: open an image, edit it, save it next door.
await stencil.load('https://raw.githubusercontent.com/andrew1407/stencil/main/bot/assets/icon.png');

// Crop by named edges; a leading '-' measures from the far side.
stencil.crop({ x1: '8%', x2: '-8%', y1: '8%', y2: '-8%' });
stencil.apply({ filter: 'sepia' });

// Style settings apply to every shape after them, in any order.
stencil.apply({ lineColor: '#1e63c8', thickness: 3, lineStyle: 'dashed' });

// Bare numbers are pixels.
stencil.setLines([
  { points: [{ x: 80, y: 330 }, { x: 205, y: 125 }, { x: 330, y: 330 }] },
], { mode: 'combine' });

await stencil.saveProjectFile();
