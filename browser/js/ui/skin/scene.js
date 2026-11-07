// The picture an empty editor gets with a skin's word: leave incognito, load the skin's image under its
// own name and lay the word over it as one step, or reopen the local project the skin named before.
// Desktop twin: ThemePainter::skinScene.
import { waitForImage, loadImageFromFile } from '../../core/image/loadFlow.js';

// `scene`: { imageName, projectName, image: { width, height }, blob(), wordLines(w, h) }.
export const createSkinScene = async (app, scene) => {
  if (app.storage?.incognito) {
    app.storage.incognito = false;
    app.updateIncognitoUI?.();
  }
  if (app.imageFilter && app.imageFilter !== 'none') app.settings?.setImageFilter?.('none');
  const previous = app.image;
  const blob = await scene.blob();
  const { width, height } = scene.image;
  // The file's name seeds the image base name, and that names the project (core/image/loadFlow.js);
  // the word rides the load, so the picture never paints without it.
  const firstStep = { imageWidth: width, imageHeight: height, lines: scene.wordLines(width, height) };
  loadImageFromFile(app, new File([blob], scene.imageName, { type: 'image/png' }), { firstStep });
  await waitForImage(app, { previous });
  return !!app.image && app.image !== previous;
};

// The local (never server-linked) project named `projectName`, reopened; false without one.
export const openSkinProject = (app, projectName) => {
  const want = projectName.toLowerCase();
  const meta = (app.storage?.store?.list?.() || [])
    .find((m) => !m.remoteId && String(m.name || '').trim().toLowerCase() === want);
  return !!meta && !!app.projectTransfer.switchToProject(meta.id);
};

// A reopened project decodes its picture too, so either way the caller waits for one.
export const settleSkinScene = async (app, scene) => {
  if (!openSkinProject(app, scene.projectName)) await createSkinScene(app, scene);
  else await waitForImage(app, { timeoutMs: 3000 });
};

// A painted picture as a PNG blob, the way a blank page reaches the load path (core/image/blankImage.js).
export const paintedBlob = ({ width, height }, paint, name) => new Promise((resolve, reject) => {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  paint(canvas.getContext('2d'));
  canvas.toBlob((blob) => (blob ? resolve(blob) : reject(new Error(`${name}: the picture did not encode`))), 'image/png');
});
