// The picture the word paints on an empty editor: leave incognito, load the grass-and-sky page
// under its own name, and lay the word over it as one step. Desktop twin: MainWindow::webcoreScene.
import { waitForImage } from '../../core/image/loadFlow.js';
import { webcoreImageBlob } from './image.js';
import { wordLines, IMAGE_NAME, PROJECT_NAME } from './rules.js';

export const createWebcoreScene = async (app) => {
  if (app.storage?.incognito) {
    app.storage.incognito = false;
    app.updateIncognitoUI?.();
  }
  if (app.imageFilter && app.imageFilter !== 'none') app.settings?.setImageFilter?.('none');
  const previous = app.image;
  const blob = await webcoreImageBlob();
  // The file's name seeds the image base name, and that names the project (core/image/loadFlow.js).
  app.loadImageFromFile(new File([blob], IMAGE_NAME, { type: 'image/png' }));
  await waitForImage(app, { previous });
  if (!app.image || app.image === previous) return false;
  const { width, height } = app.image;
  const drawn = app.export.installLayout(
    { imageWidth: width, imageHeight: height, lines: wordLines(width, height) },
    { mode: 'combine', history: true });
  app.zoomPan?.fitToWindow?.();
  return !!drawn;
};

export const openWebcoreProject = (app) => {
  const want = PROJECT_NAME.toLowerCase();
  const meta = (app.storage?.store?.list?.() || [])
    .find((m) => !m.remoteId && String(m.name || '').trim().toLowerCase() === want);
  return !!meta && !!app.switchToProject?.(meta.id);
};
