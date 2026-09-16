// ── window.stencil: the settings namespace ──────────────────────
// Extracted from stencilApi.js. A fresh accessor object per access (the setters close over
// the app); the same descriptors are also spread onto the facade itself, so
// `stencil.lineColor` and `stencil.settings.lineColor` are one setter.
import { ACCENTS, isAccent, normalizeHex, toHexColor } from '../core/accents.js';
import { motionPrefs, MOTION_MODES } from '../ui/motionPrefs.js';
import { loadVoiceSettings, saveVoiceSettings, isLanguageTag, clampSilenceMs, SILENCE_MS_MIN, SILENCE_MS_MAX } from '../llm/voiceSettings.js';
import { validateHexColor } from '../core/validation.js';
import { splitKeywords, str } from './coerce.js';

export const createSettingsFacade = ({ app, guard }) => {
  // ── Settings namespace (fresh object per access; setters close over app) ──
  const settingsAccessors = () => ({
    get lineColor() { return app.color; }, set lineColor(v) { app.settings.setColor(toHexColor(v)); },
    // Default point colour for NEW lines; '' (or null) means "follow lineColor".
    get pointColor() { return app.pointColor; },
    set pointColor(v) { app.settings.setPointColor(v == null || v === '' ? '' : toHexColor(v)); },
    get thickness() { return app.thickness; }, set thickness(v) { app.settings.setThickness(v); },
    get pointSize() { return app.pointSize; }, set pointSize(v) { app.settings.setPointSize(v); },
    get lineStyle() { return app.style; }, set lineStyle(v) { app.settings.setLineStyle(v); },
    get pointStyle() { return app.showPoints; }, set pointStyle(v) { app.settings.setShowPoints(v); },   // points visible?
    get showPoints() { return app.showPoints; }, set showPoints(v) { app.settings.setShowPoints(v); },
    get showLines() { return app.showLines; }, set showLines(v) { app.settings.setShowLines(v); },
    get filter() { return app.imageFilter; }, set filter(v) { app.settings.setImageFilter(v); },   // 'none'|'bw'|'sepia'|'invert'|'contour'|'custom'
    get compareMode() { return app.compareMode; }, set compareMode(v) { app.settings.setCompareMode(v); },   // 'none'|'original'|'vertical'|'horizontal' — hold original vs current edit
    get compareSplit() { return app.compareSplit; }, set compareSplit(v) { app.settings.setCompareSplit(v); },   // divider position 0..1 for the split modes
    get filterColor() { return app.filterColor; }, set filterColor(v) { app.settings.setFilterColor(toHexColor(v)); },
    get unit() { return app.unit; }, set unit(v) { app.settings.setUnit(v); },
    get pageSize() { return app.pageSize; }, set pageSize(v) { app.settings.setPageSize(v); },            // case-insensitive: any ISO name A0–C10 ('a3', 'b5', …) or 'custom'
    get pageWidth() { return app.customPageWidth; }, set pageWidth(v) { app.settings.setCustomPageWidth(Number(v)); },     // cm; applies when pageSize='custom'
    get pageHeight() { return app.customPageHeight; }, set pageHeight(v) { app.settings.setCustomPageHeight(Number(v)); },  // cm; applies when pageSize='custom'
    get darkTheme() { return app.theme === 'dark'; }, set darkTheme(v) { app.setTheme(v ? 'dark' : 'light'); },   // dark mode on/off
    // Brand accent: a preset key (see stencil.mainThemes) persists + syncs across tabs; a
    // hex like '#ff5623' applies to THIS page only (not saved, not synced). Anything else
    // throws. The getter returns the active custom hex if set, otherwise the preset key.
    get mainTheme() { return app.customAccent || app.accent; },
    set mainTheme(v) {
      const s = str(v).trim();
      const k = s.toLowerCase();
      if (isAccent(k)) { app.setAccent(k); return; }
      const hex = normalizeHex(s);
      if (hex) { app.setCustomAccent(hex); return; }
      throw new Error(`Unknown theme "${v}". Use a hex like #ff5623, or one of: ${ACCENTS.map((a) => a.key).join(', ')}`);
    },
    get mainThemes() { return ACCENTS.map((a) => a.key); },                                       // available accent keys
    // Active project's accent colour — the custom colour its NAME is painted in. Getter
    // returns the stored "#rrggbb" or '' (no custom colour → theme accent). Setter routes to
    // DrawingApp.setProjectColor: '' clears it, a valid hex sets it, anything else throws.
    get projectColor() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.color || '') : '';
    },
    set projectColor(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to color');
      const s = str(v).trim();
      if (!validateHexColor(s, { allowEmpty: true }).ok) throw new Error(`Invalid project color "${v}" — use a hex like #ff5623, or '' to clear`);
      app.setProjectColor(id, s);
    },
    // Active project's free-text description ('' when unset) and search keywords (string[]).
    // Both need an active project to write to (same rule as projectColor); the keywords
    // setter takes the same shapes stencil.current.keywords does (coerce.js splitKeywords).
    get description() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.description || '') : '';
    },
    set description(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to describe');
      if (app.setProjectDescription(id, str(v)) == null) throw new Error(`Could not set description on project ${id}`);
    },
    get keywords() {
      const id = app.activeProjectId;
      return id != null ? (app.storage.store.getMeta(id)?.keywords ?? []).slice() : [];
    },
    set keywords(v) {
      const id = app.activeProjectId;
      if (id == null) throw new Error('No active project to tag');
      if (app.setProjectKeywords(id, splitKeywords(v)) == null) throw new Error(`Could not set keywords on project ${id}`);
    },
    get drawMode() { return app.drawMode; }, set drawMode(v) { app.setDrawMode(String(v).toLowerCase() === 'rect' ? 'rect' : 'line'); },
    // Hold-to-draw hold/dwell delay in milliseconds (clamped 100–3000). See holdDraw.js.
    get holdDrawDelay() { return app.holdDrawDelay; }, set holdDrawDelay(v) { app.input.setHoldDrawDelay(v); },
    get allowFormulas() { return app.allowFormulas; }, set allowFormulas(v) { app.settings.setAllowFormulas(v); },
    get formulaX() { return app.formulaX; }, set formulaX(v) { app.settings.setFormula('x', v); },
    get formulaY() { return app.formulaY; }, set formulaY(v) { app.settings.setFormula('y', v); },
    // ── Motion (ui/motionPrefs.js; the Visuals modal's Motion section) ──
    // The canvas stroke animation — a new vertex flying to where it was put, its
    // landing pop and ripple. Off puts every point straight down.
    get drawingAnimations() { return motionPrefs().drawing; },
    set drawingAnimations(v) { app.settings.setMotion('drawing', !!v); },
    // How the INTERFACE moves: 'particles' | 'water' | 'fire' (the same flights as dust,
    // drops or embers), 'slide' (each surface's own plain entrance) or 'none'.
    // prefers-reduced-motion still wins.
    get motionMode() { return motionPrefs().mode; },
    set motionMode(v) { app.settings.setMotion('mode', v); },
    get motionModes() { return MOTION_MODES.slice(); },
    // Whether an open window dims and blurs what it covers. Desktop twin: the same row in
    // its Visuals dialog (support/motionPrefs.hpp modalBackdrop).
    get modalBackdrop() { return motionPrefs().backdrop; },
    set modalBackdrop(v) { app.settings.setMotion('backdrop', !!v); },
    get fillColor() { return app.defaultFillColor; }, set fillColor(v) { app.settings.setVisualColor('fill', toHexColor(v)); },
    get selectionGlow() { return app.selGlowColor; }, set selectionGlow(v) { app.settings.setVisualColor('selGlow', toHexColor(v)); },
    get hoverRing() { return app.hoverRingColor; }, set hoverRing(v) { app.settings.setVisualColor('hoverRing', toHexColor(v)); },
    get focusRing() { return app.focusRingColor; }, set focusRing(v) { app.settings.setVisualColor('focusRing', toHexColor(v)); },
    // Voice input (js/llm/voiceSettings.js — its own store, shared by dictation and voice
    // chat). Language: 'default' (English) or any BCP-47 tag like 'de-DE'; a live
    // recognizer switches at once. Silence: the pause that sends, clamped 500–10000 ms.
    get voiceInputLanguage() { return loadVoiceSettings().language; },
    set voiceInputLanguage(v) {
      const lang = str(v).trim();
      const isDefault = lang === '' || lang.toLowerCase() === 'default';
      if (!isDefault && !isLanguageTag(lang)) throw new Error(`Invalid voice input language "${v}" — use 'default' or a BCP-47 tag like en-US`);
      saveVoiceSettings({ ...loadVoiceSettings(), language: isDefault ? 'default' : lang });
    },
    get voiceSilenceMs() { return loadVoiceSettings().silenceMs; },
    set voiceSilenceMs(v) {
      if (!Number.isFinite(Number(v))) throw new Error(`Invalid voice silence "${v}" — milliseconds between ${SILENCE_MS_MIN} and ${SILENCE_MS_MAX}`);
      saveVoiceSettings({ ...loadVoiceSettings(), silenceMs: clampSilenceMs(v) });
    },
  });
  const settings = () => guard(settingsAccessors());
  return { settingsAccessors, settings };
};
