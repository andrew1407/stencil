// The drag-time shots under usecases/docs/browser/img: the drop zones a project row is dragged
// out to, and the overlay a dragged file raises. They only exist mid-gesture, so each step holds
// the drag open and shoots before ending it.
import { waitForAnimations } from '../lib/waits.mjs';

// The app arms the zones on `dragstart` and highlights from `dragover` on the document
// (projects/list/dragReorder.js), so dispatching that pair paints the same overlay a pointer does —
// including the translucent row ghost, which is a real element, not the browser's drag image.
const HOLD_DRAG = (zone) => `
  const row = document.querySelector('.project-row[data-id]');
  const r = row.getBoundingClientRect();
  const dt = new DataTransfer();
  const at = (x, y, type) => document.dispatchEvent(
    new DragEvent(type, { bubbles: true, clientX: x, clientY: y, dataTransfer: dt }));
  row.dispatchEvent(new DragEvent('dragstart',
    { bubbles: true, clientX: r.left + 40, clientY: r.top + r.height / 2, dataTransfer: dt }));
  const p = ${JSON.stringify(zone)};
  at(Math.round(innerWidth * p[0]), Math.round(innerHeight * p[1]), 'dragover');
`;

// A file dragged in from the desktop: the overlay reads the item's MIME type mid-drag
// (core/pointer/dropKind.js), so a .stencil (no type) gets the split and a .json one zone.
const HOLD_FILE_DRAG = (name, type, x) => `
  const dt = new DataTransfer();
  dt.items.add(new File(['{}'], ${JSON.stringify(name)}, { type: ${JSON.stringify(type)} }));
  for (const t of ['dragenter', 'dragover']) document.dispatchEvent(new DragEvent(t,
    { bubbles: true, cancelable: true, clientX: Math.round(innerWidth * ${x}), clientY: Math.round(innerHeight / 3), dataTransfer: dt }));
`;

// Fractions of the viewport, chosen to sit clear of the dialog card (zoneForPoint returns null
// over it): bottom 30% is Remove, above that the left half is Open here, the right Open in a new tab.
const ZONES = Object.freeze({
  here: [0.12, 0.20],
  newtab: [0.88, 0.20],
  remove: [0.50, 0.88],
});

export function makeDragSteps({ config, runner, pages }) {
  const { shared, openModal, closeModal } = pages;

  const dropZoneStep = (name, zone) => ({
    name,
    run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await page.evaluate(async (size) => {
        window.stencil.newEditor();
        await window.stencil.blank('#000000', { size });
      }, config.get('canvas.blankSize'));
      await openModal(page, 'projects', 'projects-modal-overlay');
      await page.locator('.project-row[data-id]').first().waitFor();
      await page.evaluate(HOLD_DRAG(ZONES[zone]));
      await page.locator('.project-dropzones.is-dragging').waitFor();
      await page.locator(`.pdz-${zone}.pdz-over`).waitFor();
      await waitForAnimations(page, '.project-dropzones');
      await runner.shot(page, name);
      // End the drag before the shared page moves on, or the zones sit over the next shot.
      await page.evaluate(() => document.dispatchEvent(
        new DragEvent('dragend', { bubbles: true, dataTransfer: new DataTransfer() })));
      await page.locator('.project-dropzones.is-dragging').waitFor({ state: 'detached' })
        .catch(() => {});
      await closeModal(page, 'projects-modal-overlay');
    },
  });

  // Over a blank page, so the layout zone reads as ready to draw rather than "open an image first".
  const fileDropStep = (name, file, type, x, ready) => ({
    name,
    run: async (ctx, theme) => {
      const page = await shared(ctx, theme);
      await page.evaluate(async (size) => {
        window.stencil.newEditor();
        await window.stencil.blank('#ffffff', { size });
      }, config.get('canvas.blankSize'));
      await page.evaluate(HOLD_FILE_DRAG(file, type, x));
      await page.locator(`#global-drop-overlay${ready}`).waitFor();
      await waitForAnimations(page, '#global-drop-overlay');
      // The blank page's own "Saved" toast is not what the shot is about.
      await page.evaluate(() => document.querySelectorAll('.notify-toast').forEach((t) => t.remove()));
      await runner.shot(page, name);
      await page.evaluate(() => document.dispatchEvent(
        new DragEvent('dragend', { bubbles: true, dataTransfer: new DataTransfer() })));
      await page.locator('#global-drop-overlay').waitFor({ state: 'hidden' }).catch(() => {});
    },
  });

  // One shot carries the whole overlay: all three labels are on screen, with the one under the
  // pointer lit, so a second zone would only repeat it.
  return Object.freeze([
    dropZoneStep('projects-dropzones', 'here'),
    fileDropStep('file-dropzones', 'Kitchen plan.stencil', '', 0.78, ' .drop-zone-right.drop-zone-active'),
    fileDropStep('layout-dropzone', 'kitchen-layout.json', 'application/json', 0.30, '.drop-single'),
  ]);
}
