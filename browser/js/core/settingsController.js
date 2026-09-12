import { setVal } from '../utils.js';
import { COMMIT_DEBOUNCE_MS } from '../ui/numericInput.js';
import { MOTION_MODES, setMotionPrefs, motionPrefs } from '../ui/motionPrefs.js';
import {
  applyMirror, paintFormulaToggle, paintFormulaError,
  readControl, forEachControl, paintTooltipOption, paintMotionMode, paintMotionDrawing,
} from '../ui/settingMirrors.js';
import { SETTINGS } from './settingsRegistry.js';

export { COMPARE_MODES } from './settingsRegistry.js';


// ── SettingsController: the shared editor setters ───────────────────
// Single source of truth for top-menu settings: toolbar handlers AND the console API
// (window.stencil) reach these through DrawingApp's thin delegators. Holds no state —
// back-references the app. `persist:false` is used by live-drag (input) events that
// commit on the trailing change (no write per slider tick).
export class SettingsController {
  constructor(app) {
    this.app = app;
  }

  // Registry-driven setter shared by the simple settings above. Writes the model field,
  // mirrors every bound element, then runs the declared commit (redraw always; save /
  // remoteSync / filterDirty only when persisting).
  set(key, value, { persist = true } = {}) {
    const d = SETTINGS[key];
    if (!d) throw new Error(`Unknown setting: ${key}`);
    const app = this.app;
    const v = d.parse ? d.parse(value) : value;
    if (v === undefined) return;          // parse aborted (e.g. NaN) — no-op, matches old guards
    app[d.field] = v;
    for (const m of d.mirror || []) applyMirror(m, v);
    if (d.afterSet) d.afterSet(this, v);
    if (d.redraw) app.renderer.redraw();
    if (persist) {
      if (d.filterDirty) app.filterDirty = true;   // user changed the filter → our filter wins on save
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

  // Live-apply a setting's VISUAL effect WITHOUT committing it — the dropdowns' hover
  // preview. Sets the model field and repaints only: no mirror, no afterSet, no
  // save/history/remoteSync. Restore by previewing the committed value; a bad one is
  // ignored.
  preview(key, value) {
    const d = SETTINGS[key];
    if (!d) return;
    let v;
    try { v = d.parse ? d.parse(value) : value; } catch { return; }
    if (v === undefined) return;
    this.app[d.field] = v;
    if (d.redraw) this.app.renderer.redraw();
  }

  // Divider position for the split compare modes (0..1), clamped so a sliver of each
  // side stays visible. Transient view state — redraw only, no persist/sync.
  setCompareSplit(v) {
    const n = parseFloat(v);
    if (Number.isNaN(n)) return;
    this.app.compareSplit = Math.min(0.98, Math.max(0.02, n));
    this.app.renderer.redraw();
  }

  setFilterColor(v, opts) { this.set('filterColor', v, opts); }

  setPageSize(size) { this.set('pageSize', size); }

  // Width/height are stored in cm (the model unit); the input is shown in the active
  // display unit. Pass cm from the UI handler (it converts the typed value first).
  setCustomPageWidth(cm) { this.set('customPageWidth', cm); }

  setCustomPageHeight(cm) { this.set('customPageHeight', cm); }

  setUnit(u) { this.set('unit', u); }

  // ── Formula controls (shared with #wireFormulaControls + #adoptServerFormulas) ──
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

  // Wire one pair of f(x,y) fields (toolbar or context-menu pair) so a formula applies
  // when typing SETTLES, never per keystroke — half-written states like "(x" must not
  // recompute/persist/sync or reset the transform to identity. Same delay as the numeric
  // fields (js/ui/numericInput.js); Enter/blur apply at once. `mirrorX`/`mirrorY` are the
  // twin pair a committed value reflects into; the pair being typed is never written back,
  // so the caret stays put. Returns the commit fn (for tests / programmatic flushes).
  wireFormulaInputs({ x, y, mirrorX, mirrorY }) {
    const app = this.app;
    const read = (id) => readControl(id);
    const bothValid = () => app.formula.validate(read(x), 'x') && app.formula.validate(read(y), 'y');
    let timer = null;

    const commit = () => {
      clearTimeout(timer);
      timer = null;
      const fx = read(x);
      const fy = read(y);
      // Settled and still unparseable → now it's worth flagging. The last good transform
      // stays in force, so the coordinates on screen never follow a half-written formula.
      if (!bothValid()) { this.showFormulaError(true); return; }
      this.showFormulaError(false);
      if (fx === app.formulaX && fy === app.formulaY) return;   // nothing actually changed
      app.formulaX = fx;
      app.formulaY = fy;
      setVal(mirrorX, fx);
      setVal(mirrorY, fy);
      this.refreshFormulaCoords();
      app.storage.save();
      app.remoteSync.scheduleRemoteSync();   // push the formula change to peers/server
    };

    forEachControl([x, y], (el) => {
      el.addEventListener('input', () => {
        clearTimeout(timer);
        // Typing your way back to something valid clears a stale error immediately; a wrong
        // one is only flagged once you stop, so "(x" mid-expression doesn't flash red.
        if (bothValid()) this.showFormulaError(false);
        timer = setTimeout(commit, COMMIT_DEBOUNCE_MS);
      });
      el.addEventListener('blur', () => commit());
      el.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); commit(); } });
    });
    return commit;
  }

  // Set the x or y coordinate transform. Throws on an invalid expression so the
  // console surfaces it; the UI handler catches and shows the inline error instead.
  setFormula(axis, expr) {
    const app = this.app;
    const a = axis === 'y' ? 'y' : 'x';
    const v = String(expr ?? '').trim();
    if (v && !app.formula.validate(v, a)) throw new Error(`Invalid ${a} formula: ${expr}`);
    if (a === 'x') app.formulaX = v; else app.formulaY = v;
    setVal(`formula-${a}`, v);
    setVal(`ctx-formula-${a}`, v);
    this.showFormulaError(false);
    this.refreshFormulaCoords();
    app.storage.save();
    app.remoteSync.scheduleRemoteSync();
  }

  // Toggle one tooltip section: key ∈ 'enabled' | 'page' | 'screen' | 'coords'.
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

  // ── Motion preferences (ui/motionPrefs.js) ───────────────────────────
  // key ∈ 'mode' (particles | water | fire | slide | none) | 'drawing' (the canvas stroke motion).
  // App-wide, not part of the project, so this writes the shared store rather than a
  // model field — but it is still the ONE funnel the visuals modal and the console
  // facade both come through, mirroring the dialog's controls on the way.
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
    } else {
      throw new Error(`Unknown motion setting: ${key} (use mode | drawing)`);
    }
    return motionPrefs();
  }

  // Set one "visual default" colour (shared by the visuals modal + console settings).
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
