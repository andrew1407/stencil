import { setVal } from '../../utils.js';
import { COMMIT_DEBOUNCE_MS } from '../../ui/control/numericInput.js';
import { MOTION_MODES, setMotionPrefs, motionPrefs } from '../../ui/motion/motionPrefs.js';
import { NOTIFY_CHANNELS, setNotifyChannel } from './notifyChannel.js';
import {
  applyMirror, paintFormulaToggle, paintFormulaError,
  readControl, forEachControl, paintTooltipOption, paintMotionMode, paintMotionDrawing,
  paintMotionBackdrop, paintNotifyChannel,
} from '../../ui/settings/settingMirrors.js';
import { SETTINGS } from './registry.js';
import { formulaContext } from '../parse/pageMetrics.js';

export { COMPARE_MODES } from './registry.js';


// `persist:false` is the live-drag (input) path that commits on the trailing change.
export class SettingsController {
  constructor(app) {
    this.app = app;
  }

  // Registry-driven setter: write the model field, mirror every bound element, then the
  // declared commit (redraw always; save / remoteSync / filterDirty only when persisting).
  set(key, value, { persist = true } = {}) {
    const d = SETTINGS[key];
    if (!d) throw new Error(`Unknown setting: ${key}`);
    const app = this.app;
    const v = d.parse ? d.parse(value) : value;
    if (v === undefined) return;
    app[d.field] = v;
    for (const m of d.mirror || []) applyMirror(m, v);
    if (d.afterSet) d.afterSet(this, v);
    if (d.redraw) app.renderer.redraw();
    if (persist) {
      if (d.filterDirty) app.filterDirty = true;
      if (d.save) app.storage.save();
      if (d.remoteSync) app.remoteSync.scheduleRemoteSync();
    }
  }

  setColor(v, opts) { this.set('color', v, opts); }
  setPointColor(v, opts) { this.set('pointColor', v, opts); }

  setThickness(n, opts) { this.set('thickness', n, opts); }

  setPointSize(n, opts) { this.set('pointSize', n, opts); }

  setLineStyle(s) { this.set('style', s); }

  setShowPoints(b) { this.set('showPoints', b); }

  setShowLines(b) { this.set('showLines', b); }

  setImageFilter(f) { this.set('imageFilter', f); }

  setCompareMode(m) { this.set('compareMode', m); }

  // Live-apply a setting's VISUAL effect without committing it (the dropdowns' hover
  // preview): model field + repaint only. Restore by previewing the committed value.
  preview(key, value) {
    const d = SETTINGS[key];
    if (!d) return;
    let v;
    try { v = d.parse ? d.parse(value) : value; } catch { return; }
    if (v === undefined) return;
    this.app[d.field] = v;
    if (d.redraw) this.app.renderer.redraw();
  }

  // 0..1, clamped so a sliver of each side stays visible. Transient view state — redraw only.
  setCompareSplit(v) {
    const n = parseFloat(v);
    if (Number.isNaN(n)) return;
    this.app.compareSplit = Math.min(0.98, Math.max(0.02, n));
    this.app.renderer.redraw();
  }

  setFilterColor(v, opts) { this.set('filterColor', v, opts); }

  setPageSize(size) { this.set('pageSize', size); }

  // Stored in cm (the model unit); the UI handler converts the typed value first.
  setCustomPageWidth(cm) { this.set('customPageWidth', cm); }

  setCustomPageHeight(cm) { this.set('customPageHeight', cm); }

  setUnit(u) { this.set('unit', u); }

  syncFormulaUI(checked) { paintFormulaToggle(checked); }

  showFormulaError(hasError) { paintFormulaError(hasError); }

  refreshFormulaCoords() {
    const app = this.app;
    const li = app.coordLineIdx;
    const pts = li === -1
      ? (app.currentLine ? app.currentLine.points : null)
      : (app.lines[li] ? app.lines[li].points : null);
    app.coordTable.update(pts, li);
  }

  setAllowFormulas(b) { this.set('allowFormulas', b); }

  // A formula applies when typing SETTLES, on the numeric fields' delay (js/ui/numericInput.js);
  // the pair being typed is never written back, so the caret stays put.
  wireFormulaInputs({ x, y, mirrorX, mirrorY }) {
    const app = this.app;
    const read = (id) => readControl(id);
    const ctx = () => formulaContext(app);
    const bothValid = () => app.formula.validateCtx(read(x), ctx()) && app.formula.validateCtx(read(y), ctx());
    let timer = null;

    const commit = () => {
      clearTimeout(timer);
      timer = null;
      const fx = read(x);
      const fy = read(y);
      // Settled and still unparseable → flag it; the last good transform stays in force.
      if (!bothValid()) { this.showFormulaError(true); return; }
      this.showFormulaError(false);
      if (fx === app.formulaX && fy === app.formulaY) return;
      app.formulaX = fx;
      app.formulaY = fy;
      setVal(mirrorX, fx);
      setVal(mirrorY, fy);
      this.refreshFormulaCoords();
      app.storage.save();
      app.remoteSync.scheduleRemoteSync();
    };

    forEachControl([x, y], (el) => {
      el.addEventListener('input', () => {
        clearTimeout(timer);
        // A stale error clears as soon as the text is valid; a wrong one is only flagged on settle.
        if (bothValid()) this.showFormulaError(false);
        timer = setTimeout(commit, COMMIT_DEBOUNCE_MS);
      });
      el.addEventListener('blur', () => commit());
      el.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); commit(); } });
    });
    return commit;
  }

  // Throws on an invalid expression so the console surfaces it (the UI handler shows the inline error).
  setFormula(axis, expr) {
    const app = this.app;
    const a = axis === 'y' ? 'y' : 'x';
    const v = String(expr ?? '').trim();
    if (v && !app.formula.validateCtx(v, formulaContext(app))) throw new Error(`Invalid ${a} formula: ${expr}`);
    if (a === 'x') app.formulaX = v; else app.formulaY = v;
    setVal(`formula-${a}`, v);
    setVal(`ctx-formula-${a}`, v);
    this.showFormulaError(false);
    this.refreshFormulaCoords();
    app.storage.save();
    app.remoteSync.scheduleRemoteSync();
  }

  // key ∈ 'enabled' | 'page' | 'screen' | 'coords'.
  setTooltipOption(key, on) {
    const app = this.app;
    const propMap = { enabled: 'tooltipEnabled', page: 'tooltipShowPage', screen: 'tooltipShowScreen', coords: 'tooltipShowCoords' };
    const idMap = { enabled: 'ctx-tt-enabled', page: 'ctx-tt-page', screen: 'ctx-tt-screen', coords: 'ctx-tt-coords' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown tooltip option: ${key}`);
    app[prop] = !!on;
    paintTooltipOption(idMap[key], on);
    app.storage.save();
    try { app.tooltipMgr?.refresh?.(); } catch { /* tooltip not mounted */ }
  }

  // Motion is app-wide, not part of the project: the shared store, and still the one funnel
  // the visuals modal and the console facade both come through.
  setMotion(key, value) {
    if (key === 'mode') {
      const m = String(value).trim().toLowerCase();
      if (!MOTION_MODES.includes(m))
        throw new Error(`Unknown motion mode: ${value} (use ${MOTION_MODES.join(' | ')})`);
      setMotionPrefs({ mode: m });
      paintMotionMode(m);
    } else if (key === 'drawing') {
      setMotionPrefs({ drawing: !!value });
      paintMotionDrawing(value);
    } else if (key === 'backdrop') {
      setMotionPrefs({ backdrop: !!value });
      paintMotionBackdrop(value);
    } else {
      throw new Error(`Unknown motion setting: ${key} (use mode | drawing | backdrop)`);
    }
    return motionPrefs();
  }

  // Where a notice shows, app-wide like motion: the one funnel for the modal and the facade.
  setNotifyChannel(value) {
    const c = String(value).trim().toLowerCase();
    if (!NOTIFY_CHANNELS.includes(c))
      throw new Error(`Unknown notification channel: ${value} (use ${NOTIFY_CHANNELS.join(' | ')})`);
    setNotifyChannel(c);
    paintNotifyChannel(c);
    return c;
  }

  // key ∈ 'fill' | 'selGlow' | 'hoverRing' | 'focusRing'.
  setVisualColor(key, value) {
    const app = this.app;
    const propMap = { fill: 'defaultFillColor', selGlow: 'selGlowColor', hoverRing: 'hoverRingColor', focusRing: 'focusRingColor' };
    const idMap = { fill: 'vs-fill', selGlow: 'vs-sel-glow', hoverRing: 'vs-hover-ring', focusRing: 'vs-focus-ring' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown visual color: ${key}`);
    app[prop] = String(value);
    setVal(idMap[key], app[prop]);
    app.renderer.redraw();
    app.storage.save();
  }
}
