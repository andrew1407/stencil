// Shared rig for the exportService specs: the #notify-balloon spy, the stub app factory, and
// the service imported after the DOM is installed. Extracted from exportService.test.js.
import { installDom } from './dom.js';

export const notifications = [];
const balloon = { notify: (msg, type) => notifications.push([msg, type]) };
installDom().register('notify-balloon', balloon);

export const { ExportService } = await import('../../js/core/export/exportService.js');

// A mock app carrying just what the driven paths touch. `record` collects calls to the
// shared app methods so we can assert the service delegates instead of reimplementing.
export const makeApp = (over = {}) => {
  const record = { saveHistory: 0, redraw: 0, updateButtons: 0, coordUpdate: [] };
  const app = {
    record,
    image: null,
    lines: [],
    canvas: { width: 100, height: 80 },
    confirm: async () => true,
    askAlt: async () => 'confirm',
    saveHistory() { record.saveHistory++; },
    renderer: { redraw() { record.redraw++; } },
    strokeFx: { suspend() {}, resume() {} },
    updateButtons() { record.updateButtons++; },
    coordTable: { update: (...a) => record.coordUpdate.push(a) },
    currentLayoutPayload: () => ({ lines: [] }),
    ...over,
  };
  return app;
};

export const reset = () => { notifications.length = 0; };
export const lastNote = () => notifications[notifications.length - 1];
