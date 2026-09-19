// Shared stub app for the settingsController specs: the model fields each setter mutates plus a
// `rec` of the shared save/redraw/sync calls. Extracted from settingsController.test.js.
import { installDom } from './dom.js';

installDom();

export const { SettingsController } = await import('../../js/core/settingsController.js');
export const makeApp = (over = {}) => {
  const rec = { save: 0, redraw: 0, remoteSync: 0, coordUpdate: 0 };
  return {
    rec,
    color: '#000', thickness: 1, pointSize: 1, style: 'solid',
    showPoints: true, showLines: true, imageFilter: 'none', filterColor: '#7c3aed',
    pageSize: 'A3', customPageWidth: 21, customPageHeight: 29.7, unit: 'cm',
    allowFormulas: false, formulaX: '', formulaY: '', filterDirty: false,
    coordLineIdx: -1, currentLine: null, lines: [],
    renderer: { redraw() { rec.redraw++; } },
    storage: { save() { rec.save++; } },
    coordTable: { update() { rec.coordUpdate++; } },
    remoteSync: { scheduleRemoteSync() { rec.remoteSync++; } },
    applyUnitToUI() {}, updateCoordStatus() {},
    formula: { validate: (v) => v !== 'bad' },
    tooltipMgr: { refresh() {} },
    ...over,
  };
};
